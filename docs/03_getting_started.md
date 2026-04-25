# 시작하기

## 1. 빌드

### 요구사항

- CMake 3.10+
- C++17 컴파일러 (GCC 7+, Clang 5+, MSVC 2017+)
- Boost 1.66+ (`system`, `thread` 컴포넌트)

### CMake 빌드

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### CMakeLists.txt 연동 (사용 프로젝트)

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyServer)
set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED COMPONENTS system thread)

include_directories(path/to/Rigitaeda/include)

add_executable(myserver main.cpp)
target_link_libraries(myserver Boost::system Boost::thread pthread)
```

---

## 2. Hello World — 에코 서버

가장 단순한 형태: 받은 데이터를 그대로 되돌려 보내는 에코 서버.

```cpp
#include "Rigi_Header.hpp"
#include <thread>
#include <iostream>

using namespace Rigitaeda;

// 세션 정의: OnEvent_Receive 오버라이드 (코덱 미설정 방식)
class EchoSession : public Rigi_TCPSession {
public:
    void OnEvent_Receive(char* data, size_t len) override {
        std::cout << "[수신] " << std::string(data, len) << std::endl;
        ASync_Send(data, len);  // 그대로 되돌려 보내기
    }
    void OnEvent_Close() override {
        std::cout << "[종료] " << Get_SessionIP() << std::endl;
    }
};

// 서버 정의
class EchoServerMgr : public Rigi_TCPServerMgr<EchoSession> {
public:
    void OnEvent_Accept_Session(EchoSession* session) override {
        std::cout << "[접속] " << session->Get_SessionIP() << std::endl;
    }
};

int main()
{
    Rigi_Server server;
    EchoServerMgr mgr;
    mgr.Set_Receive_Packet_Size(4096);  // 수신 버퍼 크기

    // Run은 블로킹 — 별도 스레드에서 실행
    std::thread t([&]{ server.Run(8080, 100, &mgr); });

    std::cout << "서버 시작 (포트 8080)" << std::endl;
    std::cin.get();  // 엔터 입력 시 종료

    server.Stop();
    t.join();
    return 0;
}
```

---

## 3. 코덱 사용 에코 서버

코덱을 붙이면 `OnMessage`가 완성된 메시지 단위로 호출된다.
아래는 줄바꿈(`\n`) 구분자 코덱을 사용하는 예시다.

```cpp
#include "Rigi_Header.hpp"
#include <thread>
#include <iostream>

using namespace Rigitaeda;

class LineSession : public Rigi_TCPSession {
public:
    // 코덱 설정 시 OnMessage 호출 (완성된 메시지 단위)
    void OnMessage(const char* data, size_t len) override {
        std::string line(data, len);
        std::cout << "[라인] " << line << std::endl;

        // 응답에도 구분자 자동 추가 (ASync_Send → Codec.encode 경유)
        ASync_Send(data, len);
    }
};

class LineServerMgr : public Rigi_TCPServerMgr<LineSession> {
public:
    void OnEvent_Accept_Session(LineSession* session) override {
        // 세션 수락 시 코덱 설정
        session->Set_Codec(
            std::make_unique<Rigi_Codec_Delimiter>(std::vector<char>{'\n'})
        );
    }
};
```

---

## 4. Rigi_Server 고수준 API

`Rigi_TCPServerMgr`을 직접 쓰지 않고 `Rigi_Server`의 이벤트 핸들러 방식을 사용할 수도 있다.

```cpp
#include "Rigi_Header.hpp"
#include <thread>

using namespace Rigitaeda;

int main()
{
    Rigi_Server server(4096);  // 수신 버퍼 4096바이트

    server.Add_Event_Handler_Receive([](char* data, size_t len) {
        std::cout << "[수신] " << std::string(data, len) << std::endl;
    });

    server.Add_Event_Handler_Close([]() {
        std::cout << "[세션 종료]" << std::endl;
    });

    std::thread t([&]{ server.Run(8080, 100); });
    std::cin.get();
    server.Stop();
    t.join();
    return 0;
}
```

> **참고:** 이벤트 핸들러 방식은 `Rigi_TCPSession`을 직접 상속하는 방식보다 기능이 제한된다.
> 코덱 플러그인이나 스레드 풀을 사용하려면 `Rigi_TCPServerMgr<T>` 직접 상속을 권장한다.

---

## 5. TCP 클라이언트

`Rigi_ClientTCP`로 서버에 연결할 수 있다.

```cpp
#include "Rigi_Header.hpp"

using namespace Rigitaeda;

int main()
{
    Rigi_ClientTCP client;
    
    if (client.Connect("127.0.0.1", 8080)) {
        std::string msg = "Hello Server\n";
        client.Send(msg.c_str(), msg.size());
        
        char buf[1024] = {};
        size_t received = client.Receive(buf, sizeof(buf));
        std::cout << "[응답] " << std::string(buf, received) << std::endl;
        
        client.Close();
    }
    return 0;
}
```
