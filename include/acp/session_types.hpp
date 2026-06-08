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
//
//  Wire shape:
//    { id, name, description?, category?, type: "select",
//      currentValue, options : SessionConfigSelectOptions, _meta }
//
//  SessionConfigSelectOptions = List<ConfigSelectOption>       (Ungrouped)
//                             | List<ConfigSelectGroup>        (Grouped)
//  The two arms disambiguate by whether the array items carry `group`.
//==============================================================================
struct ConfigSelectOption {
    std::string value;                // SessionConfigValueId
    std::string name;
    Maybe<std::string> description;
    Json meta = Json::object();
};
template <> struct CodecOf<ConfigSelectOption> {
    static Codec<ConfigSelectOption> get() {
        return record<ConfigSelectOption>(
            required ("value",       &ConfigSelectOption::value),
            required ("name",        &ConfigSelectOption::name),
            optional ("description", &ConfigSelectOption::description),
            meta("_meta",       &ConfigSelectOption::meta));
    }
};

struct ConfigSelectGroup {
    std::string group;                // SessionConfigGroupId
    std::string name;
    List<ConfigSelectOption> options;
    Json meta = Json::object();
};
template <> struct CodecOf<ConfigSelectGroup> {
    static Codec<ConfigSelectGroup> get() {
        return record<ConfigSelectGroup>(
            required ("group",   &ConfigSelectGroup::group),
            required ("name",    &ConfigSelectGroup::name),
            required ("options", &ConfigSelectGroup::options),
            meta("_meta",   &ConfigSelectGroup::meta));
    }
};

struct CSO_Ungrouped { List<ConfigSelectOption> items; };
struct CSO_Grouped   { List<ConfigSelectGroup>  items; };
using ConfigSelectOptions = Sum<CSO_Ungrouped, CSO_Grouped>;

template <> struct CodecOf<ConfigSelectOptions> {
    static Codec<ConfigSelectOptions> get() {
        return {
            [](const ConfigSelectOptions& v) -> Json {
                return match(v,
                    [](const CSO_Ungrouped& x){ return to_json(x.items); },
                    [](const CSO_Grouped&   x){ return to_json(x.items); });
            },
            [](const Json& j) -> ConfigSelectOptions {
                if (!j.is_array()) throw CodecError("ConfigSelectOptions: expected array");
                // Grouped iff first element has "group" key.
                if (!j.empty() && j.front().is_object() && j.front().contains("group")) {
                    return ConfigSelectOptions{CSO_Grouped{from_json<List<ConfigSelectGroup>>(j)}};
                }
                return ConfigSelectOptions{CSO_Ungrouped{from_json<List<ConfigSelectOption>>(j)}};
            }};
    }
};

struct ConfigOption {
    std::string id;                       // SessionConfigId
    std::string name;
    Maybe<std::string> description;
    Maybe<std::string> category;          // "mode" | "model" | "thought_level" | _custom
    std::string type = "select";          // only "select" today
    std::string currentValue;             // SessionConfigValueId
    ConfigSelectOptions options;
    Json meta = Json::object();
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
            required ("options",      &ConfigOption::options),
            meta("_meta",        &ConfigOption::meta));
    }
};

// Legacy alias retained for source compatibility — prefer ConfigSelectOption.
using ConfigOptionValue = ConfigSelectOption;

//==============================================================================
//  Slash commands.
//
//  AvailableCommandInput is a tagged sum (forward-compat). Today the only arm
//  is `UnstructuredCommandInput`. Discriminator key: "type". Default arm when
//  "type" is absent: unstructured.
//==============================================================================
struct UnstructuredCommandInput {
    std::string hint;
    Json meta = Json::object();
};
template <> struct CodecOf<UnstructuredCommandInput> {
    static Codec<UnstructuredCommandInput> get() {
        return record<UnstructuredCommandInput>(
            required("hint",  &UnstructuredCommandInput::hint),
            meta("_meta", &UnstructuredCommandInput::meta));
    }
};

using AvailableCommandInput = Sum<UnstructuredCommandInput>;
template <> struct CodecOf<AvailableCommandInput> {
    static Codec<AvailableCommandInput> get() {
        // Single arm today; encode without explicit "type" tag (it's the default).
        // Decode: accept either tagged or untagged.
        return {
            [](const AvailableCommandInput& v) -> Json {
                return match(v,
                    [](const UnstructuredCommandInput& x){ return to_json(x); });
            },
            [](const Json& j) -> AvailableCommandInput {
                if (!j.is_object()) throw CodecError("AvailableCommandInput: expected object");
                const std::string type = j.value("type", "unstructured");
                if (type == "unstructured")
                    return AvailableCommandInput{from_json<UnstructuredCommandInput>(j)};
                throw CodecError("AvailableCommandInput: unknown type \"" + type + "\"");
            }};
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
