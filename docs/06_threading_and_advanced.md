# 스레딩 모델 및 고급 설정

## 1. 스레딩 모델 원칙

### 단일 io_context 스레드

Rigitaeda는 **io_context를 단일 스레드**로 운용한다.

Boost.ASIO는 내부적으로 epoll(Linux) / IOCP(Windows)를 사용한다.
이 메커니즘은 단일 스레드에서 수천 개의 소켓을 동시에 감시할 수 있다.
클라이언트가 많다고 해서 io_context 스레드를 늘릴 필요가 없다.

```
[클라이언트 1]──┐
[클라이언트 2]──┤         epoll / IOCP
[클라이언트 3]──┼──→  io_context (단일 스레드)
     ...        │         수천 소켓 동시 감시
[클라이언트 N]──┘
```

### io_context 멀티스레드를 쓰지 않는 이유

`io_context.run()`을 여러 스레드에서 호출하면:
- 포트 바인딩이 중복되지는 않는다 (`bind()`/`listen()`은 run() 전에 1번만 실행됨)
- 하지만 세션 핸들러가 여러 스레드에서 동시에 실행돼 **세션 내부 상태에 락이 필요해진다**
- 코덱 내부 버퍼, 송신 큐 등 모든 곳에 동기화를 추가해야 한다
- 결국 락 오버헤드가 처리량 향상을 상쇄한다

**올바른 확장 방법:** I/O는 단일 스레드, CPU 작업은 별도 스레드 풀로 분리한다.

---

## 2. 스레드 풀 설정

### 언제 필요한가?

`OnMessage` 내에서 다음과 같은 작업을 할 때:
- DB 쿼리 / 파일 I/O
- Protobuf 파싱 + 비즈니스 로직
- 암호화/복호화
- JSON 직렬화/역직렬화

이런 작업이 io_context 스레드를 점유하면 **다른 모든 세션의 수신이 지연된다.**

### 설정 방법

```cpp
class MyServerMgr : public Rigi_TCPServerMgr<MySession> { ... };

Rigi_Server server;
MyServerMgr mgr;

mgr.Set_ThreadPool(4);  // ← Run() 전에 설정
server.Run(8080, 1000, &mgr);
```

### 동작 원리

```
io_context 스레드
  → async_read 완료
  → Codec.decode → emit(완성 메시지)
  → dispatcher.post(task)   ← 큐에 넣고 즉시 반환
  → 다음 async_read 등록    ← io_context 스레드 블로킹 없음

스레드 풀 워커
  → task() 실행
  → OnMessage(data, len)   ← 여기서 DB 쿼리 등 처리
```

### 적정 스레드 수

| 작업 유형 | 권장 스레드 수 |
|---------|-------------|
| CPU 집중 (암호화, 파싱) | `std::thread::hardware_concurrency()` |
| I/O 대기 (DB, 파일) | 2× ~ 4× 코어 수 |
| 혼합 | 4 ~ 8개 (실측 후 조정) |

---

## 3. OnMessage 스레드 안전성

스레드 풀을 사용하면 `OnMessage`가 워커 스레드에서 실행된다.
같은 세션의 `OnMessage`가 동시에 두 번 실행되지는 않는다
(TCP는 순서 보장, 수신 버퍼는 하나이므로 이전 emit이 완료된 후 다음 emit이 시작된다).

그러나 **서로 다른 세션의 OnMessage는 동시에 실행될 수 있다.**
공유 자원(전역 맵, DB 연결 풀 등)에는 반드시 동기화가 필요하다.

```cpp
// 공유 자원 접근 시 락 필요
class MySession : public Rigi_TCPSession {
    void OnMessage(const char* data, size_t len) override {
        std::lock_guard<std::mutex> lock(g_shared_mutex);
        g_shared_map[key] = value;
    }
};
```

---

## 4. ASync_Send 스레드 안전성

`ASync_Send()`는 **어느 스레드에서 호출해도 안전하다.**
내부 뮤텍스로 보호된 송신 큐를 사용하므로, OnMessage(워커 스레드)에서 호출해도 된다.

```cpp
// OnMessage (워커 스레드) → ASync_Send (안전)
void OnMessage(const char* data, size_t len) override {
    auto response = process(data, len);                          // CPU 작업
    ASync_Send(response.data(), response.size());                // 안전
}
```

`Sync_Send()`는 블로킹 송신이다. io_context 스레드에서 호출하면 다른 세션이 블로킹된다.
특별한 이유가 없으면 `ASync_Send()`를 사용한다.

---

## 5. Close 처리 주의사항

### 잘못된 패턴

```cpp
void OnMessage(const char* data, size_t len) override {
    // ...처리...
    Close();                    // ← Close 내부에서 delete this 발생 가능
    ASync_Send(resp.data(), resp.size());  // ← Use-after-free! 위험
}
```

### 올바른 패턴

```cpp
void OnMessage(const char* data, size_t len) override {
    // 응답 먼저
    ASync_Send(resp.data(), resp.size());
    // Close는 마지막에
    if (should_close) Close();
    // Close() 이후 멤버 접근 없음
}
```

---

## 6. 세션 풀 한계 초과 처리

```cpp
class MyServerMgr : public Rigi_TCPServerMgr<MySession> {
    void OnEvent_Accept_Session(MySession* session) override {
        // 이 시점에서는 이미 풀에 추가된 상태
        // Add_Session 실패 시 session 자체가 생성되지 않음 (Handle_accept 내부 처리)
        session->Set_Codec(make_unique<Rigi_Codec_HTTP1>());
    }
};

// 서버 설정 시 최대 연결 수 지정
server.Run(8080, 1000, &mgr);  // 최대 1000 세션
```

최대 연결 수 초과 시 새 소켓은 즉시 닫힌다. 에러 응답을 보내고 싶다면 `Handle_accept`를 오버라이드한다.

---

## 7. 수신 버퍼 크기 조정

수신 버퍼는 세션당 한 번 할당된다.
메시지 최대 크기보다 크게 설정해야 한다.
코덱이 있으면 여러 TCP 세그먼트를 내부적으로 합산하므로, 버퍼는 단일 TCP 세그먼트 크기면 충분하다.

```cpp
Rigi_Server server(65536);  // 수신 버퍼 64KB (생성자 인수)

// 또는 직접 지정
mgr.Set_Receive_Packet_Size(8192);  // 8KB
server.Run(8080, 1000, &mgr);
```

---

## 8. 전체 패턴 종합 예시

```cpp
#include "Rigi_Header.hpp"
#include <thread>
using namespace Rigitaeda;

class ApiSession : public Rigi_TCPSession {
public:
    void OnMessage(const char* data, size_t len) override {
        // 이 함수는 스레드 풀 워커에서 실행됨
        auto req = Rigi_HTTP_Request::parse(data, len);
        if (!req.valid) { Close(); return; }

        // DB 조회 등 블로킹 작업 안전하게 수행
        auto result = query_database(req.uri);

        auto resp = Rigi_HTTP_Response::make(200, "application/json", result);
        resp.headers["Connection"] = req.keep_alive() ? "keep-alive" : "close";
        auto raw = resp.serialize();

        ASync_Send(raw.data(), raw.size());  // 워커 스레드에서 안전

        if (!req.keep_alive()) Close();
    }
};

class ApiServerMgr : public Rigi_TCPServerMgr<ApiSession> {
    void OnEvent_Accept_Session(ApiSession* s) override {
        s->Set_Codec(make_unique<Rigi_Codec_HTTP1>());
    }
};

int main()
{
    Rigi_Server server(65536);
    ApiServerMgr mgr;

    mgr.Set_ThreadPool(8);  // I/O 대기가 있으므로 8개

    std::thread t([&]{
        server.Run(8080, 5000, &mgr);  // 최대 5000 연결
    });

    std::cout << "API 서버 시작 (포트 8080, 최대 5000 세션, 워커 8개)" << std::endl;
    std::cin.get();

    server.Stop();
    t.join();
    return 0;
}
```
