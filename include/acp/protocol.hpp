// SPDX-License-Identifier: Apache-2.0
//
// acp/protocol.hpp — every ACP method, declared once, as a type.
//
//   A method's name, params and result live in exactly one place. The
//   engine's request<M>, the reply<M>, result<M> decoding and a Router's
//   on<M> handler are all checked against these, so a call site, a handler
//   and the wire format can't disagree about a method's shape.
//
//   Direction is part of the name of the namespace:
//
//     to_agent::   the client (editor) calls these on the agent
//     to_client::  the agent calls these on the client
//
//   ACP itself is in jsonrpc-cpp's terms: a protocol is its method table.
//   There is no connection class here and no runtime; the host drives a
//   jsonrpc::Engine and dispatches with a jsonrpc::Router (see
//   docs/PROTOCOL_LIBRARIES.md in agentty).
#pragma once

#include <jsonrpc/engine.hpp>
#include <jsonrpc/method.hpp>
#include <jsonrpc/router.hpp>

#include <acp/methods.hpp>
#include <acp/updates.hpp>

namespace acp {

using jsonrpc::Method;
using jsonrpc::Note;

// ── The client calls these on the agent ─────────────────────────────────────
namespace to_agent {
using Initialize       = Method<"initialize",                 InitializeParams,      InitializeResult>;
using Authenticate     = Method<"authenticate",               AuthenticateParams,    Unit>;
using Logout           = Method<"logout",                     Unit,                  Unit>;
using SessionNew       = Method<"session/new",                NewSessionParams,      NewSessionResult>;
using SessionLoad      = Method<"session/load",               LoadSessionParams,     LoadSessionResult>;
using SessionResume    = Method<"session/resume",             ResumeSessionParams,   ResumeSessionResult>;
// Unstable in the schema; Zed and the Claude adapter use it.
using SessionFork      = Method<"session/fork",               ForkSessionParams,     NewSessionResult>;
using SessionClose     = Method<"session/close",              CloseSessionParams,    Unit>;
using SessionDelete    = Method<"session/delete",             DeleteSessionParams,   Unit>;
using SessionList      = Method<"session/list",               ListSessionsParams,    ListSessionsResult>;
using SessionPrompt    = Method<"session/prompt",             PromptParams,          PromptResult>;
using SessionSetMode   = Method<"session/set_mode",           SetModeParams,         Unit>;
using SessionSetConfig = Method<"session/set_config_option",  SetConfigOptionParams, SetConfigOptionResult>;
using SessionCancel    = Note  <"session/cancel",             CancelParams>;
}  // namespace to_agent

// ── The agent calls these on the client ─────────────────────────────────────
namespace to_client {
using SessionUpdate       = Note  <"session/update",             SessionUpdateMsg>;
using RequestPermission   = Method<"session/request_permission", RequestPermissionParams, RequestPermissionResult>;
using FsReadTextFile      = Method<"fs/read_text_file",          ReadTextFileParams,      ReadTextFileResult>;
using FsWriteTextFile     = Method<"fs/write_text_file",         WriteTextFileParams,     Unit>;
using TerminalCreate      = Method<"terminal/create",            CreateTerminalParams,    CreateTerminalResult>;
using TerminalOutput      = Method<"terminal/output",            TerminalOutputParams,    TerminalOutputResult>;
using TerminalWaitForExit = Method<"terminal/wait_for_exit",     TerminalWaitForExitParams, TerminalWaitForExitResult>;
using TerminalKill        = Method<"terminal/kill",              TerminalKillParams,      Unit>;
using TerminalRelease     = Method<"terminal/release",           TerminalReleaseParams,   Unit>;
}  // namespace to_client

// ── Either direction ────────────────────────────────────────────────────────
// Ask the peer to stop working on one of the requests it received from us
// (the stabilised $/cancel_request RFD). `id` is the request's wire id.
using CancelRequest = Note<"$/cancel_request", CancelRequestParams>;

// The engine and its vocabulary, under acp:: for protocol code.
using jsonrpc::Engine;
using jsonrpc::Router;
using jsonrpc::Effects;
using jsonrpc::Event;
using jsonrpc::Received;
using jsonrpc::Tick;
using jsonrpc::Cancel;
using jsonrpc::Closed;
using jsonrpc::Completed;
using jsonrpc::Call;
using jsonrpc::Notification;
using jsonrpc::Id;
using jsonrpc::RequestId;
using jsonrpc::RpcError;
using jsonrpc::Deadline;
using jsonrpc::step;
using jsonrpc::request;
using jsonrpc::request_raw;
using jsonrpc::reply;
using jsonrpc::result;
using jsonrpc::notify;
using jsonrpc::notify_raw;
namespace errc = jsonrpc::errc;

// ACP's own error code, in the implementation-defined range.
inline constexpr int AuthRequired = -32000;

}  // namespace acp
