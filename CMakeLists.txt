cmake_minimum_required(VERSION 2.8)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14")

project(fplog2-test)

include_directories(dependencies/include
headers)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/dependencies/lib/x64)

add_executable(${PROJECT_NAME} "sources/main.cpp"
"sources/utils.cpp"
"sources/udp_transport.cpp"
"sources/packet_router.cpp")

target_link_libraries(${PROJECT_NAME} libgtest.a)
