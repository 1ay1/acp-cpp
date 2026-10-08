// SPDX-License-Identifier: Apache-2.0
//
// acp/codec.hpp — the codec algebra, from jsonrpc-cpp.
//
//   A protocol type gets its codec by specialising acp::CodecOf<T> in
//   namespace acp; jsonrpc's codec<T>() finds it (JSONRPC_PROTOCOL_CODECS).
//   The built-in types (bool, string, numbers, Unit, Json) keep jsonrpc's.
#pragma once

#include <jsonrpc/codec.hpp>
#include <acp/core.hpp>

namespace acp {

using jsonrpc::Codec;
using jsonrpc::CodecError;

// acp's own codec table: a protocol type specialises acp::CodecOf<T> in
// namespace acp, as it always has. For a built-in type the primary template
// defers to jsonrpc's, so CodecOf<Json>, CodecOf<std::string> etc. mean the
// same thing in either namespace. JSONRPC_PROTOCOL_CODECS lets jsonrpc's
// codec<T>() find the table by argument-dependent lookup on T.
template <class T> struct CodecOf : jsonrpc::CodecOf<T> {};
JSONRPC_PROTOCOL_CODECS(CodecOf)

using jsonrpc::codec;
using jsonrpc::to_json;
using jsonrpc::from_json;
using jsonrpc::list_codec;
using jsonrpc::maybe_codec;
using jsonrpc::newtype_codec;
using jsonrpc::EnumMapping;
using jsonrpc::enum_codec;
using jsonrpc::Field;
using jsonrpc::required;
using jsonrpc::optional;
using jsonrpc::defaulted;
using jsonrpc::meta;
using jsonrpc::record;
using jsonrpc::Arm;
using jsonrpc::arm;
using jsonrpc::sum_tagged;

}  // namespace acp
