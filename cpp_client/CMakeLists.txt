cmake_minimum_required(VERSION 3.14)

project(OrderbookClient)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

#=============================================================================
# Client application
#=============================================================================
add_executable(orderbook_client
    src/main.cpp
    src/UdpMulticastReceiver.cpp
    ../src/FixMessage.cpp
)

target_include_directories(orderbook_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(orderbook_client PRIVATE
    Threads::Threads
)

target_compile_options(orderbook_client PRIVATE -Wall -Wextra -Wpedantic)

