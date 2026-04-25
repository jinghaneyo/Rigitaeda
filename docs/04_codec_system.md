# 코덱 플러그인 시스템

## 1. 개념

코덱은 **TCP 바이트 스트림과 완성된 메시지 사이의 변환기**다.

TCP는 스트림 프로토콜이다. 보낸 데이터가 여러 조각으로 분할되거나 여러 메시지가 하나로 합쳐져 도착할 수 있다.
코덱의 역할은 이 스트림에서 "메시지 하나가 완성된 시점"을 판단하는 것이다.

```
[TCP 수신]  "HEL"  → "LO\nWOR"  → "LD\n"
                ↓  Rigi_Codec_Delimiter('\n')
[emit 호출]  "HELLO"              "WORLD"
```

---

## 2. 기본 제공 코덱

### Rigi_Codec_Delimiter — 구분자 기반

지정한 바이트 시퀀스가 나타날 때마다 메시지를 분리한다.

```cpp
// 줄바꿈 구분
session->Set_Codec(make_unique<Rigi_Codec_Delimiter>(vector<char>{'\n'}));

// CRLF 구분 (텔넷 스타일)
session->Set_Codec(make_unique<Rigi_Codec_Delimiter>(vector<char>{'\r','\n'}));

// 커스텀 2바이트 구분자 (0x17 0x17)
session->Set_Codec(make_unique<Rigi_Codec_Delimiter>(vector<char>{'\x17','\x17'}));
```

**encode():** 메시지 뒤에 구분자를 자동으로 붙인다.

---

### Rigi_Codec_LengthPrefix — 길이 접두사 기반

메시지 앞에 고정 크기 헤더(빅엔디안)로 본문 길이를 기록한다.

```cpp
// 4바이트 헤더 (최대 메시지 크기 ~4GB)
session->Set_Codec(make_unique<Rigi_Codec_LengthPrefix>(4));

// 2바이트 헤더 (최대 65535바이트)
session->Set_Codec(make_unique<Rigi_Codec_LengthPrefix>(2));
```

**프레임 구조:**
```
[4바이트: 본문 길이] [본문 데이터...]
```

**encode():** 길이 헤더를 자동으로 앞에 붙인다.

---

### Rigi_Codec_FixedSize — 고정 크기 기반

항상 동일한 크기의 메시지를 기대한다. 게임 패킷처럼 고정 구조체를 사용할 때 적합하다.

```cpp
// 64바이트 고정 패킷
session->Set_Codec(make_unique<Rigi_Codec_FixedSize>(64));
```

---

## 3. 프로토콜 코덱 (내장)

| 코덱 | 헤더 | 용도 |
|------|------|------|
| `Rigi_Codec_Syslog` | `Rigi_Codec_Syslog.hpp` | Syslog TCP (RFC 3164/5424) |
| `Rigi_Codec_RESP` | `Rigi_Codec_RESP.hpp` | Redis 프로토콜 |
| `Rigi_Codec_MQTT` | `Rigi_Codec_MQTT.hpp` | MQTT 3.1.1 |
| `Rigi_Codec_HTTP1` | `Rigi_Codec_HTTP1.hpp` | HTTP/1.1 |

각 프로토콜 상세는 [05_protocols.md](./05_protocols.md) 참조.

---

## 4. 커스텀 코덱 구현

`Rigi_Codec`을 상속하고 `decode()`를 구현하면 된다.

### 예시: Head-Body-Tail 구조 패킷

```
[2바이트: 패킷 타입] [2바이트: 본문 길이] [본문...] [1바이트: 0xFF 꼬리]
```

```cpp
class MyPacketCodec : public Rigi_Codec {
public:
    void decode(const char* data, size_t len,
                std::function<void(const char*, size_t)> emit) override
    {
        // 수신 데이터를 내부 버퍼에 누적
        m_buf.insert(m_buf.end(), data, data + len);

        while (m_buf.size() >= 5) {  // 최소 헤더 크기 확인
            // 본문 길이 파싱 (빅엔디안, 오프셋 2)
            uint16_t body_len =
                (static_cast<uint8_t>(m_buf[2]) << 8) |
                 static_cast<uint8_t>(m_buf[3]);

            size_t total = 4 + body_len + 1;  // 헤더 + 본문 + 꼬리
            if (m_buf.size() < total) break;   // 아직 데이터 부족

            // 꼬리 바이트 검증
            if (static_cast<uint8_t>(m_buf[total - 1]) != 0xFF) {
                m_buf.clear();  // 프로토콜 오류 → 버퍼 리셋
                break;
            }

            emit(m_buf.data(), total);  // 완성된 패킷 전달
            m_buf.erase(m_buf.begin(),
                        m_buf.begin() + static_cast<ptrdiff_t>(total));
        }
    }

    std::vector<char> encode(const char* data, size_t len) override
    {
        // 헤더 + 데이터 + 꼬리 조립
        // (이 예시에서는 타입을 0x0001로 고정)
        std::vector<char> frame;
        frame.push_back(0x00);  // 타입 상위
        frame.push_back(0x01);  // 타입 하위
        frame.push_back(static_cast<char>(len >> 8));
        frame.push_back(static_cast<char>(len & 0xFF));
        frame.insert(frame.end(), data, data + len);
        frame.push_back(static_cast<char>(0xFF));
        return frame;
    }

private:
    std::vector<char> m_buf;
};
```

사용:
```cpp
session->Set_Codec(make_unique<MyPacketCodec>());
```

---

## 5. 코덱 동작 규칙

### decode() 구현 시 주의사항

1. **내부 버퍼를 반드시 유지하라.** TCP는 메시지를 분할해서 보낼 수 있다. 이전 호출에서 남은 데이터를 버리면 안 된다.

2. **한 번의 decode()에서 emit()이 여러 번 호출될 수 있다.** 여러 메시지가 한 TCP 세그먼트에 합쳐져 올 수 있다.

3. **emit() 내부에서 버퍼를 수정하지 마라.** emit 콜백이 반환되기 전까지 `m_buf`를 지우면 안 된다. emit 완료 후 처리된 구간을 erase한다.

4. **데이터가 불완전하면 break하고 반환하라.** 다음 decode() 호출 시 나머지 데이터가 추가된다.

### encode() 구현 시 주의사항

- 반환된 `vector<char>`가 실제 송신 데이터다. `ASync_Send`가 내부적으로 encode를 호출한다.
- 원본 데이터를 수정하지 않고 새 벡터를 반환한다.
- 기본 구현은 데이터를 그대로 복사해 반환한다 (pass-through).

---

## 6. 코덱 없이 사용 (하위 호환)

`Set_Codec()`을 호출하지 않으면 수신 데이터가 그대로 `OnEvent_Receive()`로 전달된다.
TCP 분할/조합 문제는 애플리케이션이 직접 처리해야 한다.

```cpp
// 코덱 미설정 시 호출됨 (raw 바이트 스트림)
void OnEvent_Receive(char* data, size_t len) override {
    // 여기서 직접 메시지 경계를 파악해야 함
}

// 코덱 설정 시 호출됨 (완성된 메시지 단위)
void OnMessage(const char* data, size_t len) override {
    // 항상 완성된 메시지만 들어옴
}
```
