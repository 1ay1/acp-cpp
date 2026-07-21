// SPDX-License-Identifier: Apache-2.0
//
// acp/methods.hpp — request/response payloads for every ACP method.
//
//   Each method has a Params record and (for requests) a Result record. The
//   names match the wire methods 1:1, replacing '/' with '_'. So:
//
//     initialize                          → Initialize{Params,Result}
//     authenticate                        → Authenticate{Params}        (Unit result)
//     logout                              → no params/result (Unit on both sides)
//     session/new                         → NewSession{Params,Result}
//     session/load                        → LoadSession{Params}         (Unit result)
//     session/resume                      → ResumeSession{Params,Result}
//     session/close                       → CloseSession{Params}        (Unit result)
//     session/delete                      → DeleteSession{Params}       (Unit result)
//     session/list                        → ListSessions{Params,Result}
//     session/prompt                      → Prompt{Params,Result}
//     session/cancel       (notification) → Cancel{Params}
//     session/update       (notification) → SessionUpdateMsg
//     session/set_mode                    → SetMode{Params}             (Unit result)
//     session/set_config_option           → SetConfigOption{Params,Result}
//     session/request_permission          → RequestPermission{Params,Result}
//
//     fs/read_text_file                   → ReadTextFile{Params,Result}
//     fs/write_text_file                  → WriteTextFile{Params}       (Unit result)
//
//     terminal/create                     → CreateTerminal{Params,Result}
//     terminal/output                     → TerminalOutput{Params,Result}
//     terminal/wait_for_exit              → TerminalWaitForExit{Params,Result}
//     terminal/kill                       → TerminalKill{Params}        (Unit result)
//     terminal/release                    → TerminalRelease{Params}     (Unit result)
//
//   The wire-method-name constants live in acp::method::{}.
//
#pragma once

#include <acp/updates.hpp>

namespace acp {

//==============================================================================
//  Wire-method names — single source of truth.
//==============================================================================
namespace method {
inline constexpr std::string_view Initialize       = "initialize";
inline constexpr std::string_view Authenticate     = "authenticate";
inline constexpr std::string_view Logout           = "logout";

inline constexpr std::string_view SessionNew       = "session/new";
inline constexpr std::string_view SessionLoad      = "session/load";
inline constexpr std::string_view SessionResume    = "session/resume";
inline constexpr std::string_view SessionClose     = "session/close";
inline constexpr std::string_view SessionDelete    = "session/delete";
inline constexpr std::string_view SessionList      = "session/list";
inline constexpr std::string_view SessionPrompt    = "session/prompt";
inline constexpr std::string_view SessionCancel    = "session/cancel";           // notification
inline constexpr std::string_view SessionUpdate    = "session/update";           // notification
inline constexpr std::string_view SessionSetMode   = "session/set_mode";
inline constexpr std::string_view SessionSetConfig = "session/set_config_option";
inline constexpr std::string_view RequestPermission = "session/request_permission";

inline constexpr std::string_view FsReadTextFile   = "fs/read_text_file";
inline constexpr std::string_view FsWriteTextFile  = "fs/write_text_file";

inline constexpr std::string_view TerminalCreate      = "terminal/create";
inline constexpr std::string_view TerminalOutput      = "terminal/output";
inline constexpr std::string_view TerminalWaitForExit = "terminal/wait_for_exit";
inline constexpr std::string_view TerminalKill        = "terminal/kill";
inline constexpr std::string_view TerminalRelease     = "terminal/release";

// Generic JSON-RPC request cancellation (stabilized RFD, 2026-06-29). Either
// side may send this notification to ask the peer to cancel an outstanding
// request BY its JSON-RPC id, without a method-specific cancel. This is
// orthogonal to session/cancel (which cancels a whole turn); $/cancel_request
// targets a single in-flight request id.
inline constexpr std::string_view CancelRequest       = "$/cancel_request";      // notification
} // namespace method

//==============================================================================
//  $/cancel_request  (notification)
//
//    params: { "id": <RequestId> }  — the JSON-RPC id of the request to cancel.
//    The id is left as raw Json since JSON-RPC ids may be a number or a string.
//==============================================================================
struct CancelRequestParams {
    Json id;                       // the request id to cancel (number or string)
    Json meta = Json::object();
};
template <> struct CodecOf<CancelRequestParams> {
    static Codec<CancelRequestParams> get() {
        return record<CancelRequestParams>(
            required("id",    &CancelRequestParams::id, CodecOf<Json>::get()),
            meta("_meta", &CancelRequestParams::meta));
    }
};

//==============================================================================
//  initialize
//==============================================================================
struct InitializeParams {
    int protocolVersion = kProtocolVersion;
    ClientCapabilities clientCapabilities{};
    Maybe<ImplementationInfo> clientInfo;
    Json meta = Json::object();
};
template <> struct CodecOf<InitializeParams> {
    static Codec<InitializeParams> get() {
        return record<InitializeParams>(
            defaulted("protocolVersion",    &InitializeParams::protocolVersion,    kProtocolVersion),
            defaulted("clientCapabilities", &InitializeParams::clientCapabilities, ClientCapabilities{}),
            optional ("clientInfo",         &InitializeParams::clientInfo),
            meta("_meta",              &InitializeParams::meta));
    }
};

struct InitializeResult {
    int protocolVersion = kProtocolVersion;
    AgentCapabilities agentCapabilities{};
    Maybe<ImplementationInfo> agentInfo;
    List<AuthMethod> authMethods;
    Json meta = Json::object();
};
template <> struct CodecOf<InitializeResult> {
    static Codec<InitializeResult> get() {
        return record<InitializeResult>(
            defaulted("protocolVersion",   &InitializeResult::protocolVersion,   kProtocolVersion),
            defaulted("agentCapabilities", &InitializeResult::agentCapabilities, AgentCapabilities{}),
            optional ("agentInfo",         &InitializeResult::agentInfo),
            defaulted("authMethods",       &InitializeResult::authMethods,       List<AuthMethod>{}),
            meta("_meta",             &InitializeResult::meta));
    }
};

//==============================================================================
//  authenticate / logout
//==============================================================================
struct AuthenticateParams { std::string methodId; Json meta = Json::object(); };
template <> struct CodecOf<AuthenticateParams> {
    static Codec<AuthenticateParams> get() {
        return record<AuthenticateParams>(
            required("methodId", &AuthenticateParams::methodId),
            meta("_meta",   &AuthenticateParams::meta));
    }
};

//==============================================================================
//  session/new  /  session/load  /  session/resume
//==============================================================================
struct NewSessionParams {
    std::string cwd;
    List<McpServer> mcpServers;
    Maybe<List<std::string>> additionalDirectories;
    Json meta = Json::object();
};
template <> struct CodecOf<NewSessionParams> {
    static Codec<NewSessionParams> get() {
        return record<NewSessionParams>(
            required ("cwd",                   &NewSessionParams::cwd),
            defaulted("mcpServers",            &NewSessionParams::mcpServers, List<McpServer>{}),
            optional ("additionalDirectories", &NewSessionParams::additionalDirectories),
            meta("_meta",                 &NewSessionParams::meta));
    }
};

struct NewSessionResult {
    SessionId sessionId;
    Maybe<SessionModeState>     modes;
    Maybe<List<ConfigOption>>   configOptions;
    Json meta = Json::object();
};
template <> struct CodecOf<NewSessionResult> {
    static Codec<NewSessionResult> get() {
        return record<NewSessionResult>(
            required ("sessionId",     &NewSessionResult::sessionId),
            optional ("modes",         &NewSessionResult::modes),
            optional ("configOptions", &NewSessionResult::configOptions),
            meta("_meta",         &NewSessionResult::meta));
    }
};

struct LoadSessionParams {
    SessionId sessionId;
    std::string cwd;
    List<McpServer> mcpServers;
    Maybe<List<std::string>> additionalDirectories;
    Json meta = Json::object();
};
template <> struct CodecOf<LoadSessionParams> {
    static Codec<LoadSessionParams> get() {
        return record<LoadSessionParams>(
            required ("sessionId",             &LoadSessionParams::sessionId),
            required ("cwd",                   &LoadSessionParams::cwd),
            defaulted("mcpServers",            &LoadSessionParams::mcpServers, List<McpServer>{}),
            optional ("additionalDirectories", &LoadSessionParams::additionalDirectories),
            meta("_meta",                 &LoadSessionParams::meta));
    }
};

using ResumeSessionParams = LoadSessionParams;

struct ResumeSessionResult {
    Maybe<SessionModeState>   modes;
    Maybe<List<ConfigOption>> configOptions;
    Json meta = Json::object();
};
template <> struct CodecOf<ResumeSessionResult> {
    static Codec<ResumeSessionResult> get() {
        return record<ResumeSessionResult>(
            optional ("modes",         &ResumeSessionResult::modes),
            optional ("configOptions", &ResumeSessionResult::configOptions),
            meta("_meta",         &ResumeSessionResult::meta));
    }
};

struct CloseSessionParams  { SessionId sessionId; Json meta = Json::object(); };
struct DeleteSessionParams { SessionId sessionId; Json meta = Json::object(); };
template <> struct CodecOf<CloseSessionParams> {
    static Codec<CloseSessionParams> get() {
        return record<CloseSessionParams>(
            required("sessionId", &CloseSessionParams::sessionId),
            meta("_meta",    &CloseSessionParams::meta));
    }
};
template <> struct CodecOf<DeleteSessionParams> {
    static Codec<DeleteSessionParams> get() {
        return record<DeleteSessionParams>(
            required("sessionId", &DeleteSessionParams::sessionId),
            meta("_meta",    &DeleteSessionParams::meta));
    }
};

//==============================================================================
//  session/list
//==============================================================================
struct ListSessionsParams {
    Maybe<std::string> cwd;
    Maybe<std::string> cursor;
    Json meta = Json::object();
};
template <> struct CodecOf<ListSessionsParams> {
    static Codec<ListSessionsParams> get() {
        return record<ListSessionsParams>(
            optional("cwd",    &ListSessionsParams::cwd),
            optional("cursor", &ListSessionsParams::cursor),
            meta("_meta", &ListSessionsParams::meta));
    }
};

struct ListSessionsResult {
    List<SessionInfo> sessions;
    Maybe<std::string> nextCursor;
    Json meta = Json::object();
};
template <> struct CodecOf<ListSessionsResult> {
    static Codec<ListSessionsResult> get() {
        return record<ListSessionsResult>(
            defaulted("sessions",   &ListSessionsResult::sessions, List<SessionInfo>{}),
            optional ("nextCursor", &ListSessionsResult::nextCursor),
            meta("_meta",      &ListSessionsResult::meta));
    }
};

//==============================================================================
//  session/prompt
//==============================================================================
enum class StopReason { EndTurn, MaxTokens, MaxTurnRequests, Refusal, Cancelled };
template <> struct CodecOf<StopReason> {
    static Codec<StopReason> get() {
        return enum_codec<StopReason>(
            EnumMapping<StopReason>{StopReason::EndTurn,         "end_turn"},
            EnumMapping<StopReason>{StopReason::MaxTokens,       "max_tokens"},
            EnumMapping<StopReason>{StopReason::MaxTurnRequests, "max_turn_requests"},
            EnumMapping<StopReason>{StopReason::Refusal,         "refusal"},
            EnumMapping<StopReason>{StopReason::Cancelled,       "cancelled"});
    }
};

struct PromptParams {
    SessionId sessionId;
    List<ContentBlock> prompt;
    Json meta = Json::object();
};
template <> struct CodecOf<PromptParams> {
    static Codec<PromptParams> get() {
        return record<PromptParams>(
            required ("sessionId", &PromptParams::sessionId),
            required ("prompt",    &PromptParams::prompt),
            meta("_meta",     &PromptParams::meta));
    }
};

struct PromptResult { StopReason stopReason = StopReason::EndTurn; Json meta = Json::object(); };
template <> struct CodecOf<PromptResult> {
    static Codec<PromptResult> get() {
        return record<PromptResult>(
            required("stopReason", &PromptResult::stopReason),
            meta("_meta",     &PromptResult::meta));
    }
};

//==============================================================================
//  session/cancel (notification)
//==============================================================================
struct CancelParams { SessionId sessionId; Json meta = Json::object(); };
template <> struct CodecOf<CancelParams> {
    static Codec<CancelParams> get() {
        return record<CancelParams>(
            required("sessionId", &CancelParams::sessionId),
            meta("_meta",    &CancelParams::meta));
    }
};

//==============================================================================
//  session/set_mode  /  session/set_config_option
//==============================================================================
struct SetModeParams {
    SessionId sessionId;
    SessionModeId modeId;
    Json meta = Json::object();
};
template <> struct CodecOf<SetModeParams> {
    static Codec<SetModeParams> get() {
        return record<SetModeParams>(
            required("sessionId", &SetModeParams::sessionId),
            required("modeId",    &SetModeParams::modeId),
            meta("_meta",    &SetModeParams::meta));
    }
};

struct SetConfigOptionParams {
    SessionId sessionId;
    std::string configId;
    std::string value;
    Json meta = Json::object();
};
template <> struct CodecOf<SetConfigOptionParams> {
    static Codec<SetConfigOptionParams> get() {
        return record<SetConfigOptionParams>(
            required("sessionId", &SetConfigOptionParams::sessionId),
            required("configId",  &SetConfigOptionParams::configId),
            required("value",     &SetConfigOptionParams::value),
            meta("_meta",    &SetConfigOptionParams::meta));
    }
};

struct SetConfigOptionResult { List<ConfigOption> configOptions; Json meta = Json::object(); };
template <> struct CodecOf<SetConfigOptionResult> {
    static Codec<SetConfigOptionResult> get() {
        return record<SetConfigOptionResult>(
            required("configOptions", &SetConfigOptionResult::configOptions),
            meta("_meta",        &SetConfigOptionResult::meta));
    }
};

//==============================================================================
//  session/update (notification — Agent → Client)
//==============================================================================
struct SessionUpdateMsg {
    SessionId sessionId;
    SessionUpdate update;
    Json meta = Json::object();
};
template <> struct CodecOf<SessionUpdateMsg> {
    static Codec<SessionUpdateMsg> get() {
        return record<SessionUpdateMsg>(
            required ("sessionId", &SessionUpdateMsg::sessionId),
            required ("update",    &SessionUpdateMsg::update),
            meta("_meta",     &SessionUpdateMsg::meta));
    }
};

//==============================================================================
//  session/request_permission (Agent → Client)
//==============================================================================
struct RequestPermissionParams {
    SessionId sessionId;
    ToolCallUpdate toolCall;
    List<PermissionOption> options;
    Json meta = Json::object();
};
template <> struct CodecOf<RequestPermissionParams> {
    static Codec<RequestPermissionParams> get() {
        return record<RequestPermissionParams>(
            required("sessionId", &RequestPermissionParams::sessionId),
            required("toolCall",  &RequestPermissionParams::toolCall),
            required("options",   &RequestPermissionParams::options),
            meta("_meta",    &RequestPermissionParams::meta));
    }
};

struct RequestPermissionResult { RequestPermissionOutcome outcome; Json meta = Json::object(); };
template <> struct CodecOf<RequestPermissionResult> {
    static Codec<RequestPermissionResult> get() {
        return record<RequestPermissionResult>(
            required("outcome", &RequestPermissionResult::outcome),
            meta("_meta",  &RequestPermissionResult::meta));
    }
};

//==============================================================================
//  fs/*
//==============================================================================
struct ReadTextFileParams {
    SessionId sessionId;
    std::string path;
    Maybe<std::int64_t> line;
    Maybe<std::int64_t> limit;
    Json meta = Json::object();
};
template <> struct CodecOf<ReadTextFileParams> {
    static Codec<ReadTextFileParams> get() {
        return record<ReadTextFileParams>(
            required("sessionId", &ReadTextFileParams::sessionId),
            required("path",      &ReadTextFileParams::path),
            optional("line",      &ReadTextFileParams::line),
            optional("limit",     &ReadTextFileParams::limit),
            meta("_meta",    &ReadTextFileParams::meta));
    }
};

struct ReadTextFileResult { std::string content; Json meta = Json::object(); };
template <> struct CodecOf<ReadTextFileResult> {
    static Codec<ReadTextFileResult> get() {
        return record<ReadTextFileResult>(
            required("content", &ReadTextFileResult::content),
            meta("_meta",  &ReadTextFileResult::meta));
    }
};

struct WriteTextFileParams {
    SessionId sessionId;
    std::string path;
    std::string content;
    Json meta = Json::object();
};
template <> struct CodecOf<WriteTextFileParams> {
    static Codec<WriteTextFileParams> get() {
        return record<WriteTextFileParams>(
            required("sessionId", &WriteTextFileParams::sessionId),
            required("path",      &WriteTextFileParams::path),
            required("content",   &WriteTextFileParams::content),
            meta("_meta",    &WriteTextFileParams::meta));
    }
};

//==============================================================================
//  terminal/*
//==============================================================================
struct CreateTerminalParams {
    SessionId sessionId;
    std::string command;
    List<std::string> args;
    List<KeyValue> env;
    Maybe<std::string> cwd;
    Maybe<std::int64_t> outputByteLimit;
    Json meta = Json::object();
};
template <> struct CodecOf<CreateTerminalParams> {
    static Codec<CreateTerminalParams> get() {
        return record<CreateTerminalParams>(
            required ("sessionId",       &CreateTerminalParams::sessionId),
            required ("command",         &CreateTerminalParams::command),
            defaulted("args",            &CreateTerminalParams::args, List<std::string>{}),
            defaulted("env",             &CreateTerminalParams::env,  List<KeyValue>{}),
            optional ("cwd",             &CreateTerminalParams::cwd),
            optional ("outputByteLimit", &CreateTerminalParams::outputByteLimit),
            meta("_meta",           &CreateTerminalParams::meta));
    }
};

struct CreateTerminalResult { std::string terminalId; Json meta = Json::object(); };
template <> struct CodecOf<CreateTerminalResult> {
    static Codec<CreateTerminalResult> get() {
        return record<CreateTerminalResult>(
            required("terminalId", &CreateTerminalResult::terminalId),
            meta("_meta",     &CreateTerminalResult::meta));
    }
};

// Shared shape for /output, /wait_for_exit, /kill, /release.
struct TerminalRef {
    SessionId sessionId;
    std::string terminalId;
    Json meta = Json::object();
};
template <> struct CodecOf<TerminalRef> {
    static Codec<TerminalRef> get() {
        return record<TerminalRef>(
            required("sessionId",  &TerminalRef::sessionId),
            required("terminalId", &TerminalRef::terminalId),
            meta("_meta",     &TerminalRef::meta));
    }
};
using TerminalOutputParams      = TerminalRef;
using TerminalWaitForExitParams = TerminalRef;
using TerminalKillParams        = TerminalRef;
using TerminalReleaseParams     = TerminalRef;

struct TerminalExitStatus {
    Maybe<std::int64_t> exitCode;
    Maybe<std::string>  signal;
    Json meta = Json::object();
};
template <> struct CodecOf<TerminalExitStatus> {
    static Codec<TerminalExitStatus> get() {
        return record<TerminalExitStatus>(
            optional("exitCode", &TerminalExitStatus::exitCode),
            optional("signal",   &TerminalExitStatus::signal),
            meta("_meta",   &TerminalExitStatus::meta));
    }
};

struct TerminalOutputResult {
    std::string output;
    bool truncated = false;
    Maybe<TerminalExitStatus> exitStatus;
    Json meta = Json::object();
};
template <> struct CodecOf<TerminalOutputResult> {
    static Codec<TerminalOutputResult> get() {
        return record<TerminalOutputResult>(
            required ("output",     &TerminalOutputResult::output),
            defaulted("truncated",  &TerminalOutputResult::truncated, false),
            optional ("exitStatus", &TerminalOutputResult::exitStatus),
            meta("_meta",      &TerminalOutputResult::meta));
    }
};

using TerminalWaitForExitResult = TerminalExitStatus;

} // namespace acp
