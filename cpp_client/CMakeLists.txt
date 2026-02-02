cmake_minimum_required(VERSION 3.14)

project(OrderbookClient)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(Threads REQUIRED)

#=============================================================================
# Fetch and build libzmq from source (same as exchange project)
#=============================================================================
message(STATUS "Fetching libzmq...")
FetchContent_Declare(
    libzmq
    GIT_REPOSITORY https://github.com/zeromq/libzmq.git
    GIT_TAG v4.3.5
    GIT_SHALLOW TRUE
)

set(ZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_DOCS OFF CACHE BOOL "" FORCE)
set(WITH_PERF_TOOL OFF CACHE BOOL "" FORCE)
set(ENABLE_DRAFTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(WITH_LIBSODIUM OFF CACHE BOOL "" FORCE)
set(WITH_TLS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(libzmq)

#=============================================================================
# Fetch cppzmq (header-only C++ bindings)
#=============================================================================
message(STATUS "Fetching cppzmq...")
FetchContent_Declare(
    cppzmq
    GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
    GIT_TAG v4.10.0
    GIT_SHALLOW TRUE
)
set(CPPZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cppzmq)

#=============================================================================
# Client application
#=============================================================================
add_executable(orderbook_client
    src/main.cpp
    src/UdpMulticastReceiver.cpp
)

target_include_directories(orderbook_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${cppzmq_SOURCE_DIR}
)

target_link_libraries(orderbook_client PRIVATE
    Threads::Threads
    libzmq-static
)

target_compile_options(orderbook_client PRIVATE -Wall -Wextra -Wpedantic)

