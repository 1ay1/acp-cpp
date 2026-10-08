// SPDX-License-Identifier: Apache-2.0
//
// acp/acp.hpp — the whole library.
//
//   acp-cpp is the Agent Client Protocol as types, on jsonrpc-cpp:
//
//     the algebra    core.hpp, codec.hpp        (re-exported from jsonrpc-cpp)
//     the schema     ids.hpp, content.hpp, tools.hpp, session_types.hpp,
//                    updates.hpp, caps.hpp, methods.hpp
//     the protocol   protocol.hpp: every method as a jsonrpc Method/Note type,
//                    by direction (to_agent::, to_client::)
//
//   It is a passive library: no threads, transports, futures or clocks. The
//   host owns a jsonrpc::Engine per connection and drives it:
//
//       acp::Engine e;
//       auto [id, fx] = acp::request<acp::to_agent::Initialize>(e, params);
//       ... fx = acp::step(e, acp::Received{line}); ...
//
//   and dispatches the peer's calls with an acp::Router<Ctx>.
#pragma once

#include <acp/version.hpp>
#include <acp/core.hpp>
#include <acp/codec.hpp>

#include <acp/ids.hpp>
#include <acp/content.hpp>
#include <acp/tools.hpp>
#include <acp/session_types.hpp>
#include <acp/updates.hpp>
#include <acp/caps.hpp>
#include <acp/methods.hpp>

#include <acp/protocol.hpp>
