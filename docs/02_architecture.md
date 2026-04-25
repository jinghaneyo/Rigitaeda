# 아키텍처 설계

## 1. 전체 구조

```
┌──────────────────────────────────────────────────────────────┐
│                        응용 계층                               │
│   class MySession : public Rigi_TCPSession                    │
│   void OnMessage(data, len) { /* 비즈니스 로직 */ }            │
├──────────────────────────────────────────────────────────────┤
│                    코덱 / 미들웨어 계층                          │
│   Rigi_Codec_HTTP1 / Rigi_Codec_MQTT / Rigi_Codec_RESP /     │
│   Rigi_Codec_Syslog / Rigi_Codec_LengthPrefix / 커스텀 ...    │
├──────────────────────────────────────────────────────────────┤
│                      전송 계층 (Rigitaeda)                     │
│   Rigi_TCPServerMgr ── Rigi_SessionPool ── Rigi_TCPSession   │
│              │                                    │           │
│        io_context                          async_write 큐     │
│        (epoll/IOCP)                        (deque + mutex)    │
└──────────────────────────────────────────────────────────────┘
```

---

## 2. 컴포넌트 상세

### Rigi_TCPServerMgr\<T\>

서버의 진입점 역할을 담당하는 템플릿 클래스.

```
Rigi_TCPServerMgr<MySession>
  ├─ io_context          비동기 이벤트 루프 (epoll/IOCP)
  ├─ tcp::acceptor       포트 Listen + async_accept
  ├─ Rigi_SessionPool    세션 풀 (최대 연결 수 관리)
  └─ thread_pool         OnMessage 워커 스레드 (선택)
```

**중요:** `acceptor.bind()` / `acceptor.listen()`은 `io_context.run()` 이전에 한 번만 실행된다.
멀티스레드로 `io_context.run()`을 돌려도 포트는 중복 바인딩되지 않는다.
그러나 Rigitaeda는 단일 io_context 스레드 + 별도 thread_pool 구조를 권장한다.

---

### Rigi_SessionPool

세션의 생명주기를 관리한다.

```
Add_Session(session, socket)
  ├─ 최대 연결 수 초과 시 → false 반환 (session/socket 호출부에서 해제)
  ├─ socket → session에 할당
  ├─ 수신 버퍼 할당 (Make_Receive_Packet_Size)
  ├─ Async_Receive 시작
  └─ m_sessions에 보관

Close_Session(session)
  └─ m_sessions에서 제거 + delete session
```

**규칙:** `Close_Session(this)` 호출 이후 멤버 접근 금지 (delete this 발생).
`Rigi_TCPSession::Close()` 내부에서 pool 포인터를 지역 변수로 캐싱한 뒤 nullptr로 초기화하고 호출한다.

---

### Rigi_TCPSession

세션 기반 클래스. 상속하여 사용한다.

```
Rigi_TCPSession
  ├─ SOCKET_TCP*          소켓
  ├─ Rigi_Codec*          코덱 플러그인 (unique_ptr)
  ├─ deque<vector<char>>  송신 큐
  ├─ mutex                송신 큐 보호
  ├─ function<void(fn)>   디스패처 (thread_pool 연결용)
  └─ char*                수신 버퍼
```

**데이터 흐름 (수신):**
```
async_read_some 완료
  → Handler_Receive
    → Codec.decode(raw bytes)
      → emit(완성된 메시지)
        → dispatcher 있음? → thread_pool에 post → OnMessage
                   없음? → 직접 OnMessage 호출
  → BufferClear
  → 다음 async_read_some 등록
```

**데이터 흐름 (송신):**
```
ASync_Send(data, size)
  → Codec.encode(data) → frame 생성
  → send_mutex lock
  → send_queue에 push
  → idle 상태면 do_send() 호출
    → async_write(queue.front())
      → 완료 시 do_send() 재귀 (다음 항목)
```

---

### Rigi_Codec 인터페이스

```cpp
class Rigi_Codec {
    // 수신: TCP 스트림 → 완성된 메시지 단위
    virtual void decode(const char* data, size_t len,
                        function<void(const char*, size_t)> emit) = 0;

    // 송신: 메시지 → 프레임 바이트 (기본: 그대로 통과)
    virtual vector<char> encode(const char* data, size_t len);
};
```

`decode()`는 내부 버퍼를 유지하며 TCP 분할/조합 문제를 처리한다.
완성된 메시지가 만들어질 때마다 `emit()`을 호출한다. 한 번의 decode 호출에서 emit이 여러 번 발생할 수 있다.

---

## 3. 스레딩 모델

```
[메인 스레드]
  io_context.run()   ← 이 스레드만 소켓 I/O 처리
    ├─ async_accept 완료 → Handle_accept
    ├─ async_read  완료 → Handler_Receive → Codec.decode → emit
    └─ async_write 완료 → do_send (다음 항목)

[워커 스레드 N개] (Set_ThreadPool 설정 시)
  thread_pool
    └─ OnMessage(data, len)  ← CPU 집중 작업 여기서 처리
```

**io_context 스레드에서 절대 블로킹 금지:**
DB 조회, 파일 I/O, 무거운 연산이 있으면 반드시 `Set_ThreadPool(N)`으로 분리한다.

---

## 4. 세션 생명주기

```
[Accept]
  Handle_accept
    → new MySession()
    → Set_Dispatcher (스레드 풀 있는 경우)
    → SessionPool.Add_Session()
      → socket 할당, 수신 버퍼 생성
      → Async_Receive 시작
    → OnEvent_Accept_Session(session)

[수신 중]
  Handler_Receive
    → 정상: Codec 처리 → OnMessage
    → 에러: OnEvent_Close() → Close()

[Close]
  Close(error)
    → socket.close() + delete socket
    → send_queue.clear()
    → pool 포인터 캐싱 → m_pSessionPool = nullptr
    → pool->Close_Session(this)  ← 이 시점 이후 this 접근 불가
      → m_sessions에서 제거 + delete this
```

---

## 5. 파일 구조

```
Rigitaeda/
├── include/
│   ├── Rigi_Header.hpp          단일 include 진입점
│   ├── Rigi_Def.hpp             타입 정의 (SOCKET_TCP, __in 등)
│   ├── Rigi_Server.hpp          고수준 서버 클래스
│   ├── Rigi_TCPServerMgr.hpp    서버 템플릿 (Accept 루프)
│   ├── Rigi_TCPSession.hpp      세션 기반 클래스
│   ├── Rigi_SessionPool.hpp     세션 풀
│   ├── Rigi_ClientTCP.hpp       TCP 클라이언트
│   ├── Rigi_Codec.hpp           코덱 인터페이스 + 기본 구현 3종
│   ├── Rigi_Codec_Syslog.hpp    Syslog 코덱
│   ├── Rigi_Codec_RESP.hpp      Redis RESP2 코덱
│   ├── Rigi_Codec_MQTT.hpp      MQTT 3.1.1 코덱
│   └── Rigi_Codec_HTTP1.hpp     HTTP/1.1 코덱
└── src/
    ├── Rigi_Server.cpp
    ├── Rigi_TCPSession.cpp
    └── Rigi_SessionPool.cpp
```
