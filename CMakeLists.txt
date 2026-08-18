cmake_minimum_required(VERSION 3.20...3.31)
project(acp-cpp
        VERSION       0.2.0
        DESCRIPTION   "Agent Client Protocol — modern C++20 implementation"
        LANGUAGES     CXX)

# The ACP wire protocol version is a single integer per the spec, decoupled
# from this library's semver. Bump only on breaking protocol changes.
set(ACP_PROTOCOL_VERSION 1 CACHE STRING "ACP wire protocol version")

# ACP v2 is a Draft (announced 2026-07-20). The spec REQUIRES gating v2 behind
# both version negotiation and a build feature flag, and forbids shipping it by
# default. OFF keeps the negotiated ceiling at v1 (stable). ON raises the
# ceiling so a v2-capable peer can agree on v2 (defines ACP_ENABLE_V2_DRAFT).
option(ACP_ENABLE_V2_DRAFT "Advertise/negotiate Draft ACP v2 (opt-in, not for production)" OFF)

# -----------------------------------------------------------------------------
# C++ settings
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# Optional sanitizers (ASan/UBSan together, or TSan). The threading in the RPC
# engine + stdio transport makes TSan especially valuable in CI.
# -----------------------------------------------------------------------------
set(ACP_SANITIZE "" CACHE STRING "Sanitizer: '', 'address', 'thread', or 'undefined'")
if (ACP_SANITIZE)
    if (ACP_SANITIZE STREQUAL "address")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    elseif (ACP_SANITIZE STREQUAL "thread")
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=thread)
    elseif (ACP_SANITIZE STREQUAL "undefined")
        add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=undefined)
    endif()
endif()

# -----------------------------------------------------------------------------
# Generated version.hpp — single source of truth is project(... VERSION).
# -----------------------------------------------------------------------------
set(ACP_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
    "${ACP_GENERATED_INCLUDE_DIR}/acp/version.hpp"
    @ONLY)

# -----------------------------------------------------------------------------
# Dependencies — nlohmann/json (FetchContent)
# -----------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# -----------------------------------------------------------------------------
# acp::acp — header-only interface library
# -----------------------------------------------------------------------------
add_library(acp INTERFACE)
add_library(acp::acp ALIAS acp)

target_include_directories(acp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${ACP_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>)

target_compile_features(acp INTERFACE cxx_std_20)
target_link_libraries(acp INTERFACE nlohmann_json::nlohmann_json)

# The codec algebra instantiates a deep template tree per protocol type; on
# MSVC a single TU that pulls in <acp/acp.hpp> can exceed the COFF section
# limit (C1128). /bigobj raises it. Propagated to every consumer.
target_compile_options(acp INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)

# Opt-in Draft v2 negotiation ceiling (see the ACP_ENABLE_V2_DRAFT option).
if (ACP_ENABLE_V2_DRAFT)
    target_compile_definitions(acp INTERFACE ACP_ENABLE_V2_DRAFT=1)
endif()

# -----------------------------------------------------------------------------
# Install (covers both the hand-written headers and the generated version.hpp).
# -----------------------------------------------------------------------------
include(GNUInstallDirs)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/acp/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/acp"
        FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY "${ACP_GENERATED_INCLUDE_DIR}/acp/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/acp"
        FILES_MATCHING PATTERN "*.hpp")

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
# Default ON only when acp-cpp is the top-level project (standalone). Embedded
# via add_subdirectory (e.g. by agentty) they default OFF so the parent never
# pulls acp-cpp's tests/examples into its build — each repo runs its own tests.
if(NOT DEFINED PROJECT_IS_TOP_LEVEL)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(PROJECT_IS_TOP_LEVEL ON)
    else()
        set(PROJECT_IS_TOP_LEVEL OFF)
    endif()
endif()
option(ACP_BUILD_TESTS "Build acp-cpp tests" ${PROJECT_IS_TOP_LEVEL})
if (ACP_BUILD_TESTS)
    enable_testing()

    # doctest — single test binary. Every test is a TEST_CASE auto-registered
    # into acp_tests instead of ~10 separate executables. Matches the
    # monorepo-wide single-binary test model.
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE
    )
    set(DOCTEST_WITH_TESTS OFF CACHE INTERNAL "")
    set(DOCTEST_NO_INSTALL ON CACHE INTERNAL "")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE INTERNAL "")
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)

    add_executable(acp_tests
        tests/test_main.cpp
        tests/version_smoke.cpp
        tests/kernel_smoke.cpp
        tests/content_smoke.cpp
        tests/spec_round_trip.cpp
        tests/loopback.cpp
        tests/async_prompt.cpp
        tests/engine_features.cpp
        tests/schema_conformance.cpp
        tests/cancel_request.cpp
    )
    target_link_libraries(acp_tests PRIVATE acp::acp doctest::doctest)
    if (NOT WIN32)
        target_link_libraries(acp_tests PRIVATE pthread)
    endif()
    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
    doctest_discover_tests(acp_tests)

    # bench_transport is a throughput benchmark (timing loop, not assertions);
    # kept standalone.
    add_executable(acp_bench_transport tests/bench_transport.cpp)
    target_link_libraries(acp_bench_transport PRIVATE acp::acp)
    if (NOT WIN32)
        target_link_libraries(acp_bench_transport PRIVATE pthread)
    endif()
    add_test(NAME bench_transport COMMAND acp_bench_transport)
endif()

# -----------------------------------------------------------------------------
# Examples
# -----------------------------------------------------------------------------
option(ACP_BUILD_EXAMPLES "Build acp-cpp examples" ${PROJECT_IS_TOP_LEVEL})
if (ACP_BUILD_EXAMPLES)
    # echo_agent is portable (pure stdio).
    add_executable(echo_agent examples/echo_agent.cpp)
    target_link_libraries(echo_agent PRIVATE acp::acp)
    if (NOT WIN32)
        target_link_libraries(echo_agent PRIVATE pthread)
        # minimal_client + coro_client use POSIX fork/exec; non-Windows only.
        add_executable(minimal_client examples/minimal_client.cpp)
        target_link_libraries(minimal_client PRIVATE acp::acp pthread)
        add_executable(coro_client examples/coro_client.cpp)
        target_link_libraries(coro_client PRIVATE acp::acp pthread)
    endif()
endif()
