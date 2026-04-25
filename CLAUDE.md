# Rigitaeda — 프로젝트 가이드

## 라이브러리 목적

Rigitaeda는 **순수 TCP 전송 계층 라이브러리**다.
소켓 I/O, 세션 관리, 비동기 송수신만 담당하며 프로토콜 파싱은 코덱 플러그인 계층에 위임한다.
Boost.ASIO(epoll/IOCP)를 기반으로 하며, 수천 개의 동시 연결을 단일 io_context 스레드로 처리한다.

---

## 3계층 아키텍처

```
┌─────────────────────────────────┐
│       응용 계층 (사용자 코드)       │  OnMessage 오버라이드, 비즈니스 로직
├─────────────────────────────────┤
│   코덱/미들웨어 계층 (플러그인)     │  프레이밍, 파싱, 프로토콜 변환
├─────────────────────────────────┤
│   전송 계층 (Rigitaeda 핵심)      │  TCP I/O, 세션 풀, 비동기 큐
└─────────────────────────────────┘
```

코덱은 레고 블록처럼 교체 가능하다. 핵심 코드는 건드리지 않고 `Set_Codec()`으로 꽂기만 하면 된다.

---

## 핵심 파일

| 파일 | 역할 |
|------|------|
| `include/Rigi_TCPSession.hpp` | 세션 기반 클래스 — 상속하여 `OnMessage` 오버라이드 |
| `include/Rigi_TCPServerMgr.hpp` | 서버 템플릿 — Accept 루프, 스레드 풀, 세션 생성 |
| `include/Rigi_SessionPool.hpp` | 세션 풀 — 최대 연결 수 관리, Close_Session |
| `include/Rigi_Server.hpp` | 고수준 진입점 — `Run(port, maxClient)` |
| `include/Rigi_Codec.hpp` | 코덱 인터페이스 + Delimiter / LengthPrefix / FixedSize |
| `include/Rigi_Codec_Syslog.hpp` | Syslog TCP 코덱 (RFC 3164 / 5424 / 6587) |
| `include/Rigi_Codec_RESP.hpp` | Redis RESP2 코덱 |
| `include/Rigi_Codec_MQTT.hpp` | MQTT 3.1.1 코덱 |
| `include/Rigi_Codec_HTTP1.hpp` | HTTP/1.1 코덱 (Content-Length + chunked) |
| `include/Rigi_Header.hpp` | 전체 include 단일 진입점 |

---

## 스레딩 모델

```
io_context (단일 스레드)
  ├─ async_accept   → 새 세션 생성
  ├─ async_read_some → Codec.decode → OnMessage 호출
  └─ async_write    → 내부 송신 큐에서 순차 처리

thread_pool (선택, Set_ThreadPool(N))
  └─ OnMessage를 워커 스레드로 위임 (CPU 집중 작업용)
```

**io_context를 멀티스레드로 돌리지 않는다.** epoll/IOCP는 단일 스레드로 수천 연결을 처리할 수 있다.
CPU 집중 작업이 있을 때만 `Set_ThreadPool(N)`으로 OnMessage를 분리한다.

---

## 송신 규칙

ASIO는 소켓당 `async_write` 하나만 동시에 허용한다.
`ASync_Send()`는 내부 `deque` 큐로 이 규칙을 자동 보장한다. 직접 `async_write`를 호출하지 않는다.

---

## 코덱 확장 방법

```cpp
class MyCodec : public Rigi_Codec {
    void decode(const char* data, size_t len,
                std::function<void(const char*, size_t)> emit) override
    {
        // 완성된 메시지 단위가 만들어질 때마다 emit() 호출
    }
    std::vector<char> encode(const char* data, size_t len) override
    {
        // 송신 전 프레임 추가 (구분자, 길이 헤더 등)
    }
};
```

코덱 미설정 시 `OnEvent_Receive()`가 호출된다 (하위 호환).
코덱 설정 시 완성된 메시지 단위로 `OnMessage()`가 호출된다.

---

## 세션 확장 방법

```cpp
class MySession : public Rigi_TCPSession {
    void OnMessage(const char* data, size_t len) override {
        // 완성된 메시지 처리
        ASync_Send(response.data(), response.size());
    }
    void OnEvent_Close() override { /* 연결 종료 처리 */ }
};

// 서버에 연결
class MyServer : public Rigi_TCPServerMgr<MySession> {
    void OnEvent_Accept_Session(MySession* session) override {
        session->Set_Codec(std::make_unique<Rigi_Codec_HTTP1>());
    }
};
```

---

## 설계 원칙

- **핵심은 건드리지 않는다** — 프로토콜 변경은 코덱 교체로 해결한다.
- **하위 호환 유지** — 코덱 없이 기존 방식(OnEvent_Receive)도 동작한다.
- **소유권 명확화** — 소켓/세션은 풀이 관리한다. 외부에서 직접 delete하지 않는다.
- **Close 순서** — `Close()` 내에서 `pool->Close_Session(this)` 이후 멤버 접근 금지(UB).
  pool 포인터를 지역변수로 캐싱한 뒤 nullptr로 초기화하고 호출한다.
- **io_context 스레드에서 블로킹 금지** — DB 쿼리, 파일 I/O 등은 반드시 `Set_ThreadPool`로 분리한다.
