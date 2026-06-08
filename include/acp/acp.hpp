// SPDX-License-Identifier: Apache-2.0
//
// acp/acp.hpp — single include for the whole library.
//
//   The library is structured in layers; each may also be included
//   individually if you want a smaller compile.
//
//   Layer 1 (algebra):   core.hpp, codec.hpp
//   Layer 2 (schema):    ids.hpp, content.hpp, tools.hpp,
//                        session_types.hpp, updates.hpp, caps.hpp,
//                        methods.hpp
//   Layer 3 (engine):    rpc.hpp
//   Layer 4 (transports):stdio.hpp
//   Layer 5 (facade):    agent.hpp  (use as a client),
//                        client.hpp (use as an agent)
//
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

#include <acp/rpc.hpp>
#include <acp/stdio.hpp>

#include <acp/agent.hpp>
#include <acp/client.hpp>
