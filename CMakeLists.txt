cmake_minimum_required(VERSION 3.0.0)
project(app VERSION 1.0.0)

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

if (LINUX)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -pthread")
endif()

find_package(Threads)
add_executable(app main.cpp)