// SPDX-License-Identifier: Apache-2.0
#pragma once

// Single re-export point for the JSON dependency so users can choose to
// substitute their own nlohmann::json if they already vendor it.
#if __has_include(<nlohmann/json.hpp>)
#  include <nlohmann/json.hpp>
#else
#  error "acp-cpp requires nlohmann/json. CMake fetches it automatically; if you build by hand, point to it."
#endif

namespace acp {
using Json = nlohmann::json;
} // namespace acp
