cmake_minimum_required(VERSION 3.10)
project(Rigitaeda)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m64 -Wall")

add_definitions("-D_BIN")

include_directories(./include)
include_directories(/usr/include/boost/include)
include_directories(/usr/include/glog/include)
include_directories(/usr/include/gflags)

link_directories(/usr/include/boost/lib)
link_directories(/usr/include/glog/lib)
link_directories(/usr/lib64/)

add_executable(Rigitaeda
    src/main.cpp
    src/Rigi_TCPSession.cpp
    src/Rigi_SessionPool.cpp
    src/Rigi_Server.cpp
)

target_link_libraries(Rigitaeda gflags glog pthread boost_system boost_thread)
