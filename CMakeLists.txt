cmake_minimum_required(VERSION 2.8)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

project(fplog2-test)

include_directories(dependencies/include
headers)

add_executable(${PROJECT_NAME} "sources/main.cpp"
"sources/utils.cpp"
"sources/udp_transport.cpp")
