// SPDX-License-Identifier: Apache-2.0
// The single acp-cpp test binary. Every migrated test is a doctest TEST_CASE
// auto-registered here, linking acp (header-only INTERFACE) + nlohmann once.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
