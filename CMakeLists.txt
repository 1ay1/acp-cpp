cmake_minimum_required(VERSION 3.20...3.31)
project(acp-cpp
        VERSION       0.3.0
        DESCRIPTION   "Agent Client Protocol as types, on jsonrpc-cpp"
        LANGUAGES     CXX)

# acp-cpp is a passive, header-only library: ACP's schema and method table as
# types, over jsonrpc-cpp's engine. It has no runtime (no threads, locks,
# futures, transports or clocks); the host drives a jsonrpc::Engine per
# connection. See agentty's docs/PROTOCOL_LIBRARIES.md.

# The ACP wire protocol version is a single integer per the spec, decoupled
# from this library's semver. Bump only on breaking protocol changes.
set(ACP_PROTOCOL_VERSION 1 CACHE STRING "ACP wire protocol version")

# ACP v2 is a Draft. The spec requires gating it behind both version
# negotiation and a build flag, and forbids shipping it by default.
option(ACP_ENABLE_V2_DRAFT "Advertise/negotiate Draft ACP v2 (opt-in, not for production)" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generated version.hpp — single source of truth is project(... VERSION).
set(ACP_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
    "${ACP_GENERATED_INCLUDE_DIR}/acp/version.hpp"
    @ONLY)

# Dependencies. As a subproject (agentty), the parent has already added
# jsonrpc-cpp and nlohmann_json; built alone, fetch them.
include(FetchContent)
if (NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()
if (NOT TARGET jsonrpc::jsonrpc)
    FetchContent_Declare(jsonrpc_cpp
        GIT_REPOSITORY https://github.com/1ay1/jsonrpc-cpp.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(jsonrpc_cpp)
endif()

add_library(acp INTERFACE)
add_library(acp::acp ALIAS acp)
target_include_directories(acp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${ACP_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_compile_features(acp INTERFACE cxx_std_23)
target_link_libraries(acp INTERFACE jsonrpc::jsonrpc nlohmann_json::nlohmann_json)

# The codec algebra instantiates a deep template tree per protocol type; on
# MSVC a single TU that pulls in <acp/acp.hpp> can exceed the COFF section
# limit (C1128). /bigobj raises it.
target_compile_options(acp INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)

if (ACP_ENABLE_V2_DRAFT)
    target_compile_definitions(acp INTERFACE ACP_ENABLE_V2_DRAFT=1)
endif()

# Tests: on by default only when acp-cpp is the top-level project.
option(ACP_BUILD_TESTS "Build acp-cpp tests" ${PROJECT_IS_TOP_LEVEL})
if (ACP_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE)
    set(DOCTEST_WITH_TESTS OFF CACHE INTERNAL "")
    set(DOCTEST_NO_INSTALL ON CACHE INTERNAL "")
    # doctest 2.4.11 declares cmake_minimum_required(VERSION 3.0), which CMake 4
    # refuses outright. Scoped to this one call (maya does the same).
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE INTERNAL "")
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)

    add_executable(acp_tests
        tests/test_main.cpp
        tests/version_smoke.cpp
        tests/kernel_smoke.cpp
        tests/content_smoke.cpp
        tests/spec_round_trip.cpp
        tests/schema_conformance.cpp
        tests/protocol_loopback.cpp)
    # Deliberately NOT linked with Threads::Threads and built without
    # -pthread: if the library ever needs a thread, its tests fail to link.
    target_link_libraries(acp_tests PRIVATE acp::acp doctest::doctest)
    add_test(NAME acp_tests COMMAND acp_tests)
endif()
