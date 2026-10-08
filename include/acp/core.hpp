// SPDX-License-Identifier: Apache-2.0
//
// acp/core.hpp — the algebra, from jsonrpc-cpp.
//
//   ACP's types are closed expressions in the same algebra every JSON-RPC
//   protocol here uses: Unit, products, Sum, List, Maybe, Newtype, Json.
//   That algebra lives once, in jsonrpc-cpp (docs/PROTOCOL_LIBRARIES.md §5);
//   this header re-exports it into acp:: so protocol code spells it acp::List,
//   acp::Sum and so on.
#pragma once

#include <jsonrpc/core.hpp>
#include <acp/version.hpp>

namespace acp {

using jsonrpc::Json;
using jsonrpc::Unit;
using jsonrpc::Maybe;
using jsonrpc::Just;
using jsonrpc::Nothing;
using jsonrpc::List;
using jsonrpc::Sum;
using jsonrpc::match;
using jsonrpc::StaticString;
using jsonrpc::Tag;
using jsonrpc::Newtype;

}  // namespace acp
