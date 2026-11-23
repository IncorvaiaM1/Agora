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
    src/core/order.cpp
    src/core/order_book.cpp
    src/core/matching_engine.cpp
    src/core/random_order_flow.cpp)
target_include_directories(agora_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(agora_core PRIVATE agora_warnings)

# ---------------------------------------------------------------------------
# I/O library: command log (replay) and trade log. Stream based, no sockets.
# ---------------------------------------------------------------------------
add_library(agora_io STATIC
    src/io/command_log.cpp
    src/io/trade_log.cpp)
target_link_libraries(agora_io PUBLIC agora_core PRIVATE agora_warnings)

# ---------------------------------------------------------------------------
# Network library: wire protocol, POSIX sockets, session-to-engine queues,
# gateway, market data feed and the exchange server. Plain BSD sockets and
# std::thread only, so nothing extra is needed on PACE-ICE.
# ---------------------------------------------------------------------------
add_library(agora_net STATIC
    src/net/protocol.cpp
    src/net/socket.cpp
    src/net/command_channel.cpp
    src/net/session.cpp
    src/net/gateway.cpp
    src/net/market_data_feed.cpp
    src/net/exchange_server.cpp
    src/net/client_connection.cpp)
target_link_libraries(agora_net PUBLIC agora_io Threads::Threads PRIVATE agora_warnings)

# ---------------------------------------------------------------------------
# Agents: strategy base class, runner and the strategies themselves.
# ---------------------------------------------------------------------------
add_library(agora_agents STATIC
    src/agents/agent.cpp
    src/agents/agent_factory.cpp
    src/agents/zero_intelligence_agent.cpp
    src/agents/value_agent.cpp
    src/agents/trend_agent.cpp
    src/agents/market_maker_agent.cpp)
target_link_libraries(agora_agents PUBLIC agora_net PRIVATE agora_warnings)

# ---------------------------------------------------------------------------
# Risk library (Tier 2): Black-Scholes, the CPU reference path generator, the
# Monte Carlo pricer, tail-risk measures and the live risk monitor. Plain C++,
# always built, so the reference results and their tests run on any machine.
# The CUDA path generator is added to it further down when CUDA is available.
# ---------------------------------------------------------------------------
add_library(agora_risk STATIC
    src/risk/black_scholes.cpp
    src/risk/path_generator.cpp
    src/risk/option_pricer.cpp
    src/risk/risk_metrics.cpp
    src/risk/risk_monitor.cpp)
target_link_libraries(agora_risk PUBLIC agora_net PRIVATE agora_warnings)

# ---------------------------------------------------------------------------
# Programs
# ---------------------------------------------------------------------------
add_executable(agora_replay apps/replay_main.cpp)
target_link_libraries(agora_replay PRIVATE agora_io agora_warnings)

add_executable(agora_engine_bench apps/engine_bench.cpp)
target_link_libraries(agora_engine_bench PRIVATE agora_io agora_warnings)

add_executable(agora_exchange apps/exchange_main.cpp)
target_link_libraries(agora_exchange PRIVATE agora_net agora_warnings)

add_executable(agora_agent apps/agent_main.cpp)
target_link_libraries(agora_agent PRIVATE agora_agents agora_warnings)

add_executable(agora_bench_latency apps/bench_latency.cpp)
target_link_libraries(agora_bench_latency PRIVATE agora_net agora_warnings)

add_executable(agora_risk_engine apps/risk_main.cpp)
set_target_properties(agora_risk_engine PROPERTIES OUTPUT_NAME agora_risk)
target_link_libraries(agora_risk_engine PRIVATE agora_risk agora_warnings)

# ---------------------------------------------------------------------------
# Optional tiers. Targets are added as the code lands (see PLAN.md).
# ---------------------------------------------------------------------------
if(AGORA_WITH_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        message(STATUS "Agora: CUDA risk engine enabled")

        # The GPU path generator. It is not linked to agora_warnings: those
        # flags are tuned for our own C++ and would turn warnings inside the
        # CUDA headers into errors.
        add_library(agora_risk_cuda STATIC src/risk/cuda_path_generator.cu)
        set_target_properties(agora_risk_cuda PROPERTIES CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)

        # GPU generations to compile for. The default covers the GPUs on
        # PACE-ICE (V100, RTX 6000, A100, L40S, H100/H200); the last entry also
        # embeds PTX so newer GPUs can compile it at load time. CUDA 13 dropped
        # Volta (sm_70). Override with -DAGORA_CUDA_ARCHITECTURES="90", or "native".
        if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
            set(_agora_default_archs "75-real;80-real;89-real;90")
        else()
            set(_agora_default_archs "70-real;75-real;80-real;89-real;90")
        endif()
        set(AGORA_CUDA_ARCHITECTURES "${_agora_default_archs}" CACHE STRING "CUDA architectures for the risk engine")
        set_target_properties(agora_risk_cuda PROPERTIES CUDA_ARCHITECTURES "${AGORA_CUDA_ARCHITECTURES}")

        # The kernels use cuRAND's device API, which is header only; only the
        # CUDA runtime library has to be linked. Statically, so the programs
        # start without `module load cuda` and the CPU parts run on any node.
        find_package(CUDAToolkit REQUIRED)
        target_link_libraries(agora_risk_cuda PUBLIC CUDA::cudart_static)
        target_include_directories(agora_risk_cuda PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
        # Everything that links the CUDA generator also learns that it exists.
        target_compile_definitions(agora_risk_cuda PUBLIC AGORA_HAS_CUDA=1)
        target_link_libraries(agora_risk_engine PRIVATE agora_risk_cuda)
    else()
        message(WARNING "Agora: no CUDA compiler found, building the risk engine without its GPU path generator")
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
        tests/test_order.cpp
        tests/test_order_book.cpp
        tests/test_matching_engine.cpp
        tests/test_determinism.cpp
        tests/test_protocol.cpp
        tests/test_queues.cpp
        tests/test_agents.cpp
        tests/test_risk.cpp
        tests/test_exchange.cpp)
    target_include_directories(agora_tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    target_link_libraries(agora_tests PRIVATE agora_agents agora_risk agora_warnings)
    if(TARGET agora_risk_cuda)
        # Runs the GPU generator through the same checks as the CPU one.
        target_link_libraries(agora_tests PRIVATE agora_risk_cuda)
    endif()
    add_test(NAME agora_tests COMMAND agora_tests)
endif()
