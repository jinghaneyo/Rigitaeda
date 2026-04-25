# Rigitaeda — 프로젝트 개요

## 1. 라이브러리란?

Rigitaeda는 C++17 기반의 **비동기 TCP 전송 계층 라이브러리**다.

Boost.ASIO의 epoll(Linux) / IOCP(Windows) 위에서 동작하며, 수천 개의 동시 연결을 단일 스레드로 처리한다.
라이브러리 자체는 TCP 바이트 스트림의 송수신만 책임지고, 프로토콜 해석은 **코덱 플러그인**에 완전히 위임한다.

---

## 2. 핵심 가치

### 관심사 분리
전송(Transport) — 코덱(Codec) — 응용(Application) 세 계층이 명확히 분리된다.
각 계층은 독립적으로 교체·확장할 수 있다.

### 레고 블록 방식의 프로토콜 조합
```
session->Set_Codec(make_unique<Rigi_Codec_HTTP1>());      // HTTP 서버
session->Set_Codec(make_unique<Rigi_Codec_MQTT>());       // MQTT 브로커
session->Set_Codec(make_unique<Rigi_Codec_RESP>());       // Redis 호환 서버
session->Set_Codec(make_unique<Rigi_Codec_Syslog>());     // Syslog 수집기
session->Set_Codec(make_unique<Rigi_Codec_LengthPrefix>(4)); // 커스텀 바이너리
```
코드 한 줄로 프로토콜을 바꿀 수 있다. 핵심 전송 코드는 수정하지 않는다.

### 하위 호환
코덱을 설정하지 않으면 기존 `OnEvent_Receive()` 방식이 그대로 동작한다.
점진적 마이그레이션이 가능하다.

### 안전한 비동기 송신
ASIO 소켓은 동시에 `async_write` 하나만 허용한다.
`ASync_Send()`는 내부 큐를 통해 이 제약을 자동으로 보장한다.

---

## 3. 설계 목표

| 목표 | 설명 |
|------|------|
| 고성능 | 단일 io_context + epoll/IOCP로 C10K 수준 처리 |
| 확장성 | 코덱 인터페이스만 구현하면 모든 프로토콜 지원 |
| 안전성 | 세션 풀 관리, Use-after-free 방지, 송신 큐 직렬화 |
| 이식성 | Windows(IOCP) / Linux(epoll) 동일 코드 동작 |
| 단순성 | `Rigi_Header.hpp` 하나만 include하면 전체 기능 사용 |

---

## 4. 적합한 사용 사례

- 커스텀 바이너리 프로토콜 게임 서버
- IoT 디바이스 MQTT 브로커
- 내부 서비스 간 TCP 통신 프레임워크
- Syslog 수집 서버
- Redis 프로토콜 호환 캐시 서버
- HTTP API 서버 (경량)

---

## 5. 의존성

| 라이브러리 | 버전 | 용도 |
|-----------|------|------|
| Boost.ASIO | 1.66+ | 비동기 I/O, 소켓, 스레드 풀 |
| C++ 표준 | C++17 | `std::unique_ptr`, `std::function`, `std::deque` 등 |

Protobuf, OpenSSL 등 추가 의존성 없음.
