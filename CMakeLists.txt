PROJECT(Rigitaeda)

cmake_minimum_required(VERSION 2.8)

set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_FLAGS "-m64")
add_definitions("-Wall -std=c++11 -D_BIN")

#find_package(boost REQUIRED system thread asio)
#find_package (glog REQUIRED)
#find_package(gflags REQUIRED)

include_directories(${GLOG_INCLUDE_DIRS} ${GFLAGS_INCLUDE_DIRS})

INCLUDE_DIRECTORIES(./include)
INCLUDE_DIRECTORIES(/usr/include/boost/include)
LINK_DIRECTORIES(/usr/include/boost/lib)
INCLUDE_DIRECTORIES(/usr/include/glog/include)
LINK_DIRECTORIES(/usr/include/glog/lib)
INCLUDE_DIRECTORIES(/usr/include/gflags)
LINK_DIRECTORIES(/usr/lib64/)

ADD_EXECUTABLE(Rigitaeda 
	src/main.cpp
	include/Rigi_TCPServerMgr.hpp
	src/Rigi_TCPSession.cpp
	src/Rigi_SessionPool.cpp
	src/Rigi_Server.cpp
	)

TARGET_LINK_LIBRARIES( Rigitaeda gflags glog pthread boost_system boost_thread )
