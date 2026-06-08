// SPDX-License-Identifier: Apache-2.0
//
// acp/session_types.hpp — MCP server configuration, modes, config options,
//                         slash commands, usage, session info.
//
//     McpServer ≅ StdioMcpServer + HttpMcpServer + SseMcpServer
//                 (discriminator: the "type" field; absent ⇒ "stdio")
//
//     SessionMode      ≅ { id; name; description? }
//     SessionModeState ≅ { currentModeId; availableModes : List SessionMode }
//
//     ConfigOption     ≅ { id; name; description?; category?; type;
//                          currentValue; options : List ConfigOptionValue }
//
//     AvailableCommand ≅ { name; description; input? : { hint } }
//
//     Cost             ≅ { amount : ℝ; currency : ISO4217 }
//     SessionInfo      ≅ { sessionId; cwd; additionalDirectories?; title?;
//                          updatedAt?; meta }
//
#pragma once

#include <acp/tools.hpp>

namespace acp {

//==============================================================================
//  KeyValue — generic { name; value } record, used for env vars & HTTP headers.
//==============================================================================
struct KeyValue {
    std::string name;
    std::string value;
};
template <> struct CodecOf<KeyValue> {
    static Codec<KeyValue> get() {
        return record<KeyValue>(
            required("name",  &KeyValue::name),
            required("value", &KeyValue::value));
    }
};
using EnvVariable = KeyValue;
using HttpHeader  = KeyValue;

//==============================================================================
//  MCP server configurations.
//==============================================================================
struct StdioMcpServer {
    std::string name;
    std::string command;             // absolute path per spec
    List<std::string> args;
    List<EnvVariable> env;
};
struct HttpMcpServer {
    std::string name;
    std::string url;
    List<HttpHeader> headers;
};
struct SseMcpServer {
    std::string name;
    std::string url;
    List<HttpHeader> headers;
};
using McpServer = Sum<StdioMcpServer, HttpMcpServer, SseMcpServer>;

template <> struct CodecOf<StdioMcpServer> {
    static Codec<StdioMcpServer> get() {
        return record<StdioMcpServer>(
            required ("name",    &StdioMcpServer::name),
            required ("command", &StdioMcpServer::command),
            defaulted("args",    &StdioMcpServer::args, List<std::string>{}),
            defaulted("env",     &StdioMcpServer::env,  List<EnvVariable>{}));
    }
};
template <> struct CodecOf<HttpMcpServer> {
    static Codec<HttpMcpServer> get() {
        return record<HttpMcpServer>(
            required ("name",    &HttpMcpServer::name),
            required ("url",     &HttpMcpServer::url),
            defaulted("headers", &HttpMcpServer::headers, List<HttpHeader>{}));
    }
};
template <> struct CodecOf<SseMcpServer> {
    static Codec<SseMcpServer> get() {
        return record<SseMcpServer>(
            required ("name",    &SseMcpServer::name),
            required ("url",     &SseMcpServer::url),
            defaulted("headers", &SseMcpServer::headers, List<HttpHeader>{}));
    }
};

// McpServer needs custom dispatch because the stdio variant has no explicit
// "type" tag on the wire (it's the default).
template <> struct CodecOf<McpServer> {
    static Codec<McpServer> get() {
        return {
            [](const McpServer& v) -> Json {
                return match(v,
                    [](const StdioMcpServer& x) {
                        Json j = to_json(x);
                        // omit "type" entirely — spec keeps stdio implicit
                        return j;
                    },
                    [](const HttpMcpServer& x) {
                        Json j = to_json(x); j["type"] = "http"; return j;
                    },
                    [](const SseMcpServer& x) {
                        Json j = to_json(x); j["type"] = "sse";  return j;
                    });
            },
            [](const Json& j) -> McpServer {
                if (!j.is_object()) throw CodecError("McpServer: expected object");
                const std::string type = j.value("type", "stdio");
                if (type == "http") return McpServer{from_json<HttpMcpServer>(j)};
                if (type == "sse")  return McpServer{from_json<SseMcpServer>(j)};
                return McpServer{from_json<StdioMcpServer>(j)};
            }};
    }
};

//==============================================================================
//  Session modes (legacy, but still v1).
//==============================================================================
struct SessionMode {
    SessionModeId id;
    std::string name;
    Maybe<std::string> description;
};
template <> struct CodecOf<SessionMode> {
    static Codec<SessionMode> get() {
        return record<SessionMode>(
            required ("id",          &SessionMode::id),
            required ("name",        &SessionMode::name),
            optional ("description", &SessionMode::description));
    }
};

struct SessionModeState {
    SessionModeId currentModeId;
    List<SessionMode> availableModes;
};
template <> struct CodecOf<SessionModeState> {
    static Codec<SessionModeState> get() {
        return record<SessionModeState>(
            required("currentModeId",  &SessionModeState::currentModeId),
            required("availableModes", &SessionModeState::availableModes));
    }
};

//==============================================================================
//  Session config options (the modern, preferred replacement for modes).
//==============================================================================
struct ConfigOptionValue {
    std::string value;
    std::string name;
    Maybe<std::string> description;
};
template <> struct CodecOf<ConfigOptionValue> {
    static Codec<ConfigOptionValue> get() {
        return record<ConfigOptionValue>(
            required ("value",       &ConfigOptionValue::value),
            required ("name",        &ConfigOptionValue::name),
            optional ("description", &ConfigOptionValue::description));
    }
};

struct ConfigOption {
    std::string id;
    std::string name;
    Maybe<std::string> description;
    Maybe<std::string> category;    // "mode" | "model" | "thought_level" | _custom
    std::string type = "select";    // only "select" today
    std::string currentValue;
    List<ConfigOptionValue> options;
};
template <> struct CodecOf<ConfigOption> {
    static Codec<ConfigOption> get() {
        return record<ConfigOption>(
            required ("id",           &ConfigOption::id),
            required ("name",         &ConfigOption::name),
            optional ("description",  &ConfigOption::description),
            optional ("category",     &ConfigOption::category),
            defaulted("type",         &ConfigOption::type, std::string{"select"}),
            required ("currentValue", &ConfigOption::currentValue),
            required ("options",      &ConfigOption::options));
    }
};

//==============================================================================
//  Slash commands.
//==============================================================================
struct AvailableCommandInput { std::string hint; };
template <> struct CodecOf<AvailableCommandInput> {
    static Codec<AvailableCommandInput> get() {
        return record<AvailableCommandInput>(
            required("hint", &AvailableCommandInput::hint));
    }
};

struct AvailableCommand {
    std::string name;
    std::string description;
    Maybe<AvailableCommandInput> input;
};
template <> struct CodecOf<AvailableCommand> {
    static Codec<AvailableCommand> get() {
        return record<AvailableCommand>(
            required ("name",        &AvailableCommand::name),
            required ("description", &AvailableCommand::description),
            optional ("input",       &AvailableCommand::input));
    }
};

//==============================================================================
//  Usage & cost.
//==============================================================================
struct Cost {
    double amount = 0.0;
    std::string currency;       // ISO 4217 e.g. "USD"
};
template <> struct CodecOf<Cost> {
    static Codec<Cost> get() {
        return record<Cost>(
            required("amount",   &Cost::amount),
            required("currency", &Cost::currency));
    }
};

//==============================================================================
//  SessionInfo (used by session/list and session_info_update).
//==============================================================================
struct SessionInfo {
    SessionId sessionId;
    std::string cwd;
    Maybe<List<std::string>> additionalDirectories;
    Maybe<std::string> title;
    Maybe<std::string> updatedAt;     // ISO 8601
    Json meta = Json::object();
};
template <> struct CodecOf<SessionInfo> {
    static Codec<SessionInfo> get() {
        return record<SessionInfo>(
            required ("sessionId",             &SessionInfo::sessionId),
            required ("cwd",                   &SessionInfo::cwd),
            optional ("additionalDirectories", &SessionInfo::additionalDirectories),
            optional ("title",                 &SessionInfo::title),
            optional ("updatedAt",             &SessionInfo::updatedAt),
            meta("_meta",                 &SessionInfo::meta));
    }
};

} // namespace acp
