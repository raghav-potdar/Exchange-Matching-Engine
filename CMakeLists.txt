cmake_minimum_required(VERSION 3.10)

project(Orderbook)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(orderbook src/main.cpp)
target_include_directories(orderbook PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(orderbook PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_options(orderbook PRIVATE -fsanitize=address -fsanitize=undefined)