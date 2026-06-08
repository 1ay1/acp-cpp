cmake_minimum_required(VERSION 3.20)
project(acp-cpp
        VERSION       0.2.0
        DESCRIPTION   "Agent Client Protocol — modern C++20 implementation"
        LANGUAGES     CXX)

# The ACP wire protocol version is a single integer per the spec, decoupled
# from this library's semver. Bump only on breaking protocol changes.
set(ACP_PROTOCOL_VERSION 1 CACHE STRING "ACP wire protocol version")

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
option(ACP_BUILD_TESTS "Build acp-cpp tests" ON)
if (ACP_BUILD_TESTS)
    enable_testing()
    add_executable(acp_version_smoke tests/version_smoke.cpp)
    target_link_libraries(acp_version_smoke PRIVATE acp::acp)
    add_test(NAME version_smoke COMMAND acp_version_smoke)

    add_executable(acp_kernel_smoke tests/kernel_smoke.cpp)
    target_link_libraries(acp_kernel_smoke PRIVATE acp::acp)
    add_test(NAME kernel_smoke COMMAND acp_kernel_smoke)

    add_executable(acp_content_smoke tests/content_smoke.cpp)
    target_link_libraries(acp_content_smoke PRIVATE acp::acp)
    add_test(NAME content_smoke COMMAND acp_content_smoke)

    add_executable(acp_spec_round_trip tests/spec_round_trip.cpp)
    target_link_libraries(acp_spec_round_trip PRIVATE acp::acp)
    add_test(NAME spec_round_trip COMMAND acp_spec_round_trip)

    add_executable(acp_loopback tests/loopback.cpp)
    target_link_libraries(acp_loopback PRIVATE acp::acp)
    if (NOT WIN32)
        target_link_libraries(acp_loopback PRIVATE pthread)
    endif()
    add_test(NAME loopback COMMAND acp_loopback)

    add_executable(acp_async_prompt tests/async_prompt.cpp)
    target_link_libraries(acp_async_prompt PRIVATE acp::acp)
    if (NOT WIN32)
        target_link_libraries(acp_async_prompt PRIVATE pthread)
    endif()
    add_test(NAME async_prompt COMMAND acp_async_prompt)

    add_executable(acp_engine_features tests/engine_features.cpp)
    target_link_libraries(acp_engine_features PRIVATE acp::acp)
    if (NOT WIN32)
        target_link_libraries(acp_engine_features PRIVATE pthread)
    endif()
    add_test(NAME engine_features COMMAND acp_engine_features)

    add_executable(acp_schema_conformance tests/schema_conformance.cpp)
    target_link_libraries(acp_schema_conformance PRIVATE acp::acp)
    add_test(NAME schema_conformance COMMAND acp_schema_conformance)
endif()

# -----------------------------------------------------------------------------
# Examples
# -----------------------------------------------------------------------------
option(ACP_BUILD_EXAMPLES "Build acp-cpp examples" ON)
if (ACP_BUILD_EXAMPLES)
    add_executable(echo_agent     examples/echo_agent.cpp)
    add_executable(minimal_client examples/minimal_client.cpp)
    target_link_libraries(echo_agent     PRIVATE acp::acp)
    target_link_libraries(minimal_client PRIVATE acp::acp)
    if (NOT WIN32)
        target_link_libraries(echo_agent     PRIVATE pthread)
        target_link_libraries(minimal_client PRIVATE pthread)
        # Coroutine example uses POSIX fork/exec; gate on non-Windows.
        add_executable(coro_client examples/coro_client.cpp)
        target_link_libraries(coro_client PRIVATE acp::acp pthread)
    endif()
endif()
