// SPDX-License-Identifier: Apache-2.0
//
// acp/ids.hpp — opaque identifier newtypes shared across the spec.
//
//   Each id is a Newtype<Tag, std::string>: nominally distinct in C++, a bare
//   string on the wire. This rules out (at compile time) mixing a SessionId
//   with a ToolCallId, etc.
//
#pragma once

#include <acp/codec.hpp>

namespace acp {

// Phantom tags — empty structs whose only purpose is to be distinct types.
struct SessionIdTag;
struct ToolCallIdTag;
struct SessionModeIdTag;
struct MessageIdTag;

using SessionId     = Newtype<SessionIdTag,     std::string>;
using ToolCallId    = Newtype<ToolCallIdTag,    std::string>;
using SessionModeId = Newtype<SessionModeIdTag, std::string>;
using MessageId     = Newtype<MessageIdTag,     std::string>;

// JSON-RPC id is intentionally NOT a Newtype: per JSON-RPC 2.0 it may be either
// a string OR a number. We keep it as raw Json for that polymorphism.
using RpcId = Json;

} // namespace acp
