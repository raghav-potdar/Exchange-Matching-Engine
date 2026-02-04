cmake_minimum_required(VERSION 3.14)

project(Exchange-Matching-Engine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable FetchContent
include(FetchContent)

# Find required packages
find_package(Threads REQUIRED)

#=============================================================================
# Fetch and build libzmq from source
#=============================================================================
message(STATUS "Fetching libzmq...")
FetchContent_Declare(
    libzmq
    GIT_REPOSITORY https://github.com/zeromq/libzmq.git
    GIT_TAG v4.3.5
    GIT_SHALLOW TRUE
)

# Configure libzmq build options
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

# Don't build cppzmq tests
set(CPPZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cppzmq)

#=============================================================================
# Main project
#=============================================================================

# Include directories
include_directories(include)

# Recursively find all source files in src/ directory
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c")

# Recursively find all header files in include/ directory  
file(GLOB_RECURSE HEADERS "include/*.hpp" "include/*.h")

# Print discovered files for debugging
message(STATUS "Found source files: ${SOURCES}")
message(STATUS "Found header files: ${HEADERS}")

# Create executable with all discovered source files
add_executable(exchange ${SOURCES} ${HEADERS})

# Include directories for the target
target_include_directories(exchange PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${cppzmq_SOURCE_DIR}
)

# Link libraries
target_link_libraries(exchange PRIVATE 
    Threads::Threads
    libzmq-static
)

# Compiler options
target_compile_options(exchange PRIVATE -Wall -Wextra -Wpedantic)

# Sanitizers (optional - comment out for production builds)
option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
if(ENABLE_SANITIZERS)
    target_compile_options(exchange PRIVATE -fsanitize=address -fsanitize=undefined)
    target_link_options(exchange PRIVATE -fsanitize=address -fsanitize=undefined)
endif()