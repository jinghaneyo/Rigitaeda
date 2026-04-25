# 내장 프로토콜 레퍼런스

## 1. Syslog (`Rigi_Codec_Syslog.hpp`)

### 개요

RFC 3164 (BSD Syslog) 및 RFC 5424 (현대 Syslog)를 지원한다.
TCP Syslog 프레이밍(RFC 6587)의 두 가지 방식을 모두 처리한다.

| 프레이밍 방식 | 형식 | 설명 |
|-------------|------|------|
| Newline | `<MSG>\n` | 전통적인 줄바꿈 구분 |
| OctetCounting | `<LENGTH> <MSG>` | 길이 접두사 (권장) |
| Auto (기본) | 자동 감지 | 첫 바이트가 숫자면 OctetCounting |

### Syslog 수집 서버

```cpp
#include "Rigi_Header.hpp"
using namespace Rigitaeda;

class SyslogSession : public Rigi_TCPSession {
public:
    void OnMessage(const char* data, size_t len) override {
        auto msg = Rigi_Syslog_Message::parse(data, len);
        if (!msg.valid) return;

        printf("[%s][%s] %s: %s\n",
            Rigi_Syslog_Message::facility_name(msg.facility),
            Rigi_Syslog_Message::severity_name(msg.severity),
            msg.hostname.c_str(),
            msg.message.c_str());
    }
};

class SyslogServerMgr : public Rigi_TCPServerMgr<SyslogSession> {
    void OnEvent_Accept_Session(SyslogSession* s) override {
        s->Set_Codec(make_unique<Rigi_Codec_Syslog>());  // Auto 감지
    }
};
```

### Syslog 메시지 전송 (클라이언트 역할)

```cpp
// OctetCounting 프레이밍으로 RFC 3164 메시지 전송
auto codec = make_unique<Rigi_Codec_Syslog>(Rigi_Codec_Syslog::Framing::OctetCounting);
session->Set_Codec(std::move(codec));

auto msg = Rigi_Syslog_Message::encode(
    1,              // facility: user
    6,              // severity: Info
    "myhost",       // hostname
    "myapp",        // app_name
    "서버 시작 완료"  // message
);
session->ASync_Send(msg.data(), msg.size());
```

### Rigi_Syslog_Message 구조

| 필드 | 타입 | 설명 |
|------|------|------|
| `valid` | bool | 파싱 성공 여부 |
| `priority` | int | raw PRI 값 |
| `facility` | int | 0-23 (facility 코드) |
| `severity` | int | 0-7 (0=Emergency, 7=Debug) |
| `version` | int | 0=RFC3164, 1=RFC5424 |
| `timestamp` | string | 타임스탬프 문자열 |
| `hostname` | string | 호스트명 |
| `app_name` | string | 애플리케이션명 |
| `message` | string | 실제 로그 메시지 |

---

## 2. Redis RESP2 (`Rigi_Codec_RESP.hpp`)

### 개요

Redis Serialization Protocol v2를 지원한다.
Redis 호환 서버를 구현하거나 Redis에 직접 연결하는 클라이언트를 만들 때 사용한다.

### Redis 호환 서버 예시

```cpp
#include "Rigi_Header.hpp"
using namespace Rigitaeda;

class RedisSession : public Rigi_TCPSession {
    std::map<std::string, std::string> m_store;

public:
    void OnMessage(const char* data, size_t len) override {
        auto cmd = Rigi_RESP_Value::parse(data, len);
        if (!cmd.is_array() || cmd.array.empty()) return;

        std::string op = cmd.array[0].str;
        // 대소문자 통일
        for (char& c : op) c = toupper(c);

        std::vector<char> resp;

        if (op == "PING") {
            resp = Rigi_RESP_Value::encode_simple("PONG");

        } else if (op == "SET" && cmd.array.size() >= 3) {
            m_store[cmd.array[1].str] = cmd.array[2].str;
            resp = Rigi_RESP_Value::encode_simple("OK");

        } else if (op == "GET" && cmd.array.size() >= 2) {
            auto it = m_store.find(cmd.array[1].str);
            resp = (it != m_store.end())
                   ? Rigi_RESP_Value::encode_bulk(it->second)
                   : Rigi_RESP_Value::encode_null_bulk();

        } else if (op == "DEL" && cmd.array.size() >= 2) {
            int64_t n = m_store.erase(cmd.array[1].str);
            resp = Rigi_RESP_Value::encode_integer(n);

        } else {
            resp = Rigi_RESP_Value::encode_error("ERR unknown command");
        }

        ASync_Send(resp.data(), resp.size());
    }
};

class RedisServerMgr : public Rigi_TCPServerMgr<RedisSession> {
    void OnEvent_Accept_Session(RedisSession* s) override {
        s->Set_Codec(make_unique<Rigi_Codec_RESP>());
    }
};
```

### 인코딩 헬퍼 요약

```cpp
// 커맨드 (클라이언트 → 서버)
Rigi_RESP_Value::encode_command({"SET", "key", "value"})
Rigi_RESP_Value::encode_command({"GET", "key"})
Rigi_RESP_Value::encode_command({"PING"})

// 응답 (서버 → 클라이언트)
Rigi_RESP_Value::encode_simple("OK")          // +OK\r\n
Rigi_RESP_Value::encode_error("ERR msg")      // -ERR msg\r\n
Rigi_RESP_Value::encode_integer(42)           // :42\r\n
Rigi_RESP_Value::encode_bulk("hello")         // $5\r\nhello\r\n
Rigi_RESP_Value::encode_null_bulk()           // $-1\r\n
```

### Rigi_RESP_Value 타입 판별

```cpp
auto val = Rigi_RESP_Value::parse(data, len);

val.is_null()    // $-1 또는 *-1
val.is_ok()      // +OK
val.is_error()   // - 타입
val.is_int()     // : 타입 → val.integer
val.is_string()  // + 또는 $ 타입 → val.str
val.is_array()   // * 타입 → val.array[i]
```

---

## 3. MQTT 3.1.1 (`Rigi_Codec_MQTT.hpp`)

### 개요

MQTT(Message Queuing Telemetry Transport) 3.1.1을 지원한다.
IoT 디바이스와의 통신 또는 MQTT 브로커 구현에 사용한다.

### MQTT 브로커 기본 골격

```cpp
#include "Rigi_Header.hpp"
using namespace Rigitaeda;
using namespace Rigi_MQTT;

class MQTTSession : public Rigi_TCPSession {
    std::string m_client_id;

public:
    void OnMessage(const char* data, size_t len) override {
        auto pkt = Packet::parse(data, len);

        switch (pkt.type()) {
        case CONNECT: {
            // CONNACK 응답 (세션 없음, 접속 허용)
            auto ack = build_connack(ACCEPTED, false);
            ASync_Send(ack.data(), ack.size());
            break;
        }
        case PUBLISH: {
            std::string topic = pkt.topic();
            std::string msg   = pkt.message();
            printf("[PUBLISH] %s: %s\n", topic.c_str(), msg.c_str());

            // QoS 1이면 PUBACK 응답
            if (pkt.qos() == 1) {
                auto ack = build_puback(pkt.packet_id());
                ASync_Send(ack.data(), ack.size());
            }
            break;
        }
        case SUBSCRIBE: {
            // SUBACK 응답 (QoS 0 허용)
            auto ack = build_suback(pkt.packet_id(), 0x00);
            ASync_Send(ack.data(), ack.size());
            break;
        }
        case PINGREQ: {
            auto resp = build_pingresp();
            ASync_Send(resp.data(), resp.size());
            break;
        }
        case DISCONNECT:
            Close();
            break;
        }
    }
};

class MQTTServerMgr : public Rigi_TCPServerMgr<MQTTSession> {
    void OnEvent_Accept_Session(MQTTSession* s) override {
        s->Set_Codec(make_unique<Rigi_Codec_MQTT>());
    }
};
```

### 패킷 빌더 요약

```cpp
// 연결
build_connect("client_id", "user", "pass", 60, true)
build_connack(Rigi_MQTT::ACCEPTED)

// 발행
build_publish("sensor/temp", "23.5", /*qos*/0, /*retain*/false)
build_publish("cmd/led", "ON", /*qos*/1, /*retain*/false, /*dup*/false, /*packet_id*/1)
build_puback(packet_id)

// 구독
build_subscribe(packet_id, "sensor/#", /*qos*/0)
build_suback(packet_id, 0x00)  // 0x00=QoS0, 0x01=QoS1, 0x80=실패

// Keep-alive
build_pingreq()
build_pingresp()

// 연결 종료
build_disconnect()
```

### Packet 멤버

```cpp
auto pkt = Rigi_MQTT::Packet::parse(data, len);

pkt.type()        // PacketType 열거값
pkt.flags()       // 하위 4비트
pkt.qos()         // PUBLISH: QoS 레벨 (0/1/2)
pkt.dup()         // PUBLISH: 재전송 여부
pkt.retain()      // PUBLISH: retain 플래그
pkt.topic()       // PUBLISH: 토픽 문자열
pkt.message()     // PUBLISH: 페이로드 문자열
pkt.packet_id()   // PUBLISH(QoS>0), PUBACK, SUBSCRIBE 등의 패킷 ID

// CONNACK 전용
pkt.session_present()
pkt.return_code()
```

---

## 4. HTTP/1.1 (`Rigi_Codec_HTTP1.hpp`)

### 개요

HTTP/1.1 요청/응답 처리를 지원한다.
`Content-Length` 및 `Transfer-Encoding: chunked` 바디를 모두 처리한다.

### HTTP API 서버 예시

```cpp
#include "Rigi_Header.hpp"
using namespace Rigitaeda;

class HttpSession : public Rigi_TCPSession {
public:
    void OnMessage(const char* data, size_t len) override {
        auto req = Rigi_HTTP_Request::parse(data, len);
        if (!req.valid) return;

        Rigi_HTTP_Response resp;

        if (req.method == "GET" && req.uri == "/health") {
            resp = Rigi_HTTP_Response::make(200, "application/json",
                                            R"({"status":"ok"})");

        } else if (req.method == "POST" && req.uri == "/echo") {
            std::string body(req.body.begin(), req.body.end());
            resp = Rigi_HTTP_Response::make(200,
                req.header("content-type"), body);

        } else {
            resp = Rigi_HTTP_Response::make(404, "text/plain", "Not Found");
        }

        // Connection: keep-alive 처리
        resp.headers["Connection"] = req.keep_alive() ? "keep-alive" : "close";

        auto raw = resp.serialize();
        ASync_Send(raw.data(), raw.size());

        if (!req.keep_alive()) Close();
    }
};

class HttpServerMgr : public Rigi_TCPServerMgr<HttpSession> {
    void OnEvent_Accept_Session(HttpSession* s) override {
        s->Set_Codec(make_unique<Rigi_Codec_HTTP1>());
    }
};
```

### Rigi_HTTP_Request

```cpp
auto req = Rigi_HTTP_Request::parse(data, len);

req.valid            // 파싱 성공 여부
req.method           // "GET", "POST", "PUT", "DELETE" ...
req.uri              // "/path?query=value"
req.version          // "HTTP/1.1"
req.headers          // map<string,string> (소문자 키)
req.body             // vector<char> (chunked 자동 디청크)

req.header("content-type")   // 헤더 값 조회 (없으면 "")
req.keep_alive()             // Connection: keep-alive 여부
```

### Rigi_HTTP_Response

```cpp
// 간편 생성
auto resp = Rigi_HTTP_Response::make(200, "application/json", R"({"ok":true})");

// 직접 구성
Rigi_HTTP_Response resp;
resp.status = 201;
resp.reason = "Created";
resp.headers["Location"] = "/resource/123";
resp.headers["Content-Type"] = "application/json";
std::string body = R"({"id":123})";
resp.body.assign(body.begin(), body.end());

// 직렬화 (Content-Length 자동 추가)
auto raw = resp.serialize();
session->ASync_Send(raw.data(), raw.size());
```

### 지원되는 상태 코드

100, 101, 200, 201, 204, 206, 301, 302, 304,
400, 401, 403, 404, 405, 408, 409, 413, 429,
500, 502, 503, 504
