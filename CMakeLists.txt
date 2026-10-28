# Agora: multithreaded exchange, agent market simulator, CUDA risk engine and OpenGL viewer.
# ECE 6122 final project, Michael Incorvaia.
#
# Build configurations:
#   Full (default):  cmake -S . -B build
#   Headless (PACE): cmake -S . -B build -DAGORA_WITH_GL=OFF
#   No GPU:          cmake -S . -B build -DAGORA_WITH_CUDA=OFF -DAGORA_WITH_GL=OFF

cmake_minimum_required(VERSION 3.16)

project(Agora
    VERSION 0.1.0
    DESCRIPTION "Exchange, agent market, CUDA risk engine and OpenGL viewer"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(AGORA_WITH_CUDA   "Build the CUDA Monte Carlo risk engine (Tier 2)" ON)
option(AGORA_WITH_GL     "Build the OpenGL depth viewer (Tier 3)"          ON)
option(AGORA_BUILD_TESTS "Build unit tests"                                ON)
option(AGORA_WERROR      "Treat compiler warnings as errors"               ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Warnings (the rubric deducts for compiling with warnings)
# ---------------------------------------------------------------------------
add_library(agora_warnings INTERFACE)
if(MSVC)
    target_compile_options(agora_warnings INTERFACE /W4 $<$<BOOL:${AGORA_WERROR}>:/WX>)
else()
    target_compile_options(agora_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        $<$<BOOL:${AGORA_WERROR}>:-Werror>)
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Core library: order book and matching engine. No sockets, no threads, no I/O.
# ---------------------------------------------------------------------------
add_library(agora_core STATIC
    src/core/order.cpp)
target_include_directories(agora_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(agora_core PRIVATE agora_warnings)

# ---------------------------------------------------------------------------
# Optional tiers. Targets are added as the code lands (see PLAN.md).
# ---------------------------------------------------------------------------
if(AGORA_WITH_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        message(STATUS "Agora: CUDA risk engine enabled")
    else()
        message(WARNING "Agora: no CUDA compiler found, building without the risk engine")
        set(AGORA_WITH_CUDA OFF)
    endif()
endif()

if(AGORA_WITH_GL)
    message(STATUS "Agora: OpenGL viewer enabled (requires GLFW and GLEW)")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(AGORA_BUILD_TESTS)
    enable_testing()
    add_executable(agora_tests
        tests/test_main.cpp
        tests/test_order.cpp)
    target_include_directories(agora_tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    target_link_libraries(agora_tests PRIVATE agora_core agora_warnings)
    add_test(NAME agora_tests COMMAND agora_tests)
endif()
