// SPDX-License-Identifier: Apache-2.0
//
// acp/updates.hpp — the SessionUpdate coproduct (the streaming channel).
//
//   SessionUpdate ≅
//        UserMessageChunk     { content; messageId? }
//      + AgentMessageChunk    { content; messageId? }
//      + AgentThoughtChunk    { content; messageId? }
//      + Plan                 { entries }
//      + ToolCall             ToolCall                       (flattened)
//      + ToolCallUpdate       ToolCallUpdate                 (flattened)
//      + AvailableCmdsUpdate  { availableCommands }
//      + CurrentModeUpdate    { modeId }
//      + ConfigOptionUpdate   { configOptions }
//      + SessionInfoUpdate    { title?; updatedAt?; meta }
//      + UsageUpdate          { used; size; cost? }
//
//   Discriminator key: "sessionUpdate".
//
//   The ToolCall and ToolCallUpdate arms are flattened: they reuse the
//   existing ToolCall(Update) codec and merge in the discriminator. This is
//   safe because both encode to an object and neither uses the key
//   "sessionUpdate".
//
#pragma once

#include <acp/caps.hpp>

namespace acp {

//==============================================================================
//  Arm structs.
//==============================================================================
struct SU_UserMessageChunk   { ContentBlock content; Maybe<MessageId> messageId; };
struct SU_AgentMessageChunk  { ContentBlock content; Maybe<MessageId> messageId; };
struct SU_AgentThoughtChunk  { ContentBlock content; Maybe<MessageId> messageId; };

struct SU_Plan               { List<PlanEntry> entries; };

struct SU_ToolCall           { ToolCall       toolCall; };
struct SU_ToolCallUpdate     { ToolCallUpdate update;   };

struct SU_AvailableCommands  { List<AvailableCommand> availableCommands; };
struct SU_CurrentMode        { SessionModeId currentModeId; };
struct SU_ConfigOptions      { List<ConfigOption> configOptions; };

struct SU_SessionInfo {
    Maybe<std::string> title;
    Maybe<std::string> updatedAt;
    Json meta = Json::object();
};

struct SU_Usage {
    std::int64_t used = 0;
    std::int64_t size = 0;
    Maybe<Cost> cost;
};

using SessionUpdate = Sum<
    SU_UserMessageChunk,
    SU_AgentMessageChunk,
    SU_AgentThoughtChunk,
    SU_Plan,
    SU_ToolCall,
    SU_ToolCallUpdate,
    SU_AvailableCommands,
    SU_CurrentMode,
    SU_ConfigOptions,
    SU_SessionInfo,
    SU_Usage>;

//==============================================================================
//  Codecs for each arm.
//==============================================================================
template <> struct CodecOf<SU_UserMessageChunk> {
    static Codec<SU_UserMessageChunk> get() {
        return record<SU_UserMessageChunk>(
            required("content",   &SU_UserMessageChunk::content),
            optional("messageId", &SU_UserMessageChunk::messageId));
    }
};
template <> struct CodecOf<SU_AgentMessageChunk> {
    static Codec<SU_AgentMessageChunk> get() {
        return record<SU_AgentMessageChunk>(
            required("content",   &SU_AgentMessageChunk::content),
            optional("messageId", &SU_AgentMessageChunk::messageId));
    }
};
template <> struct CodecOf<SU_AgentThoughtChunk> {
    static Codec<SU_AgentThoughtChunk> get() {
        return record<SU_AgentThoughtChunk>(
            required("content",   &SU_AgentThoughtChunk::content),
            optional("messageId", &SU_AgentThoughtChunk::messageId));
    }
};
template <> struct CodecOf<SU_Plan> {
    static Codec<SU_Plan> get() {
        return record<SU_Plan>(required("entries", &SU_Plan::entries));
    }
};

// Flattening: SU_ToolCall encodes by reusing ToolCall's codec verbatim.
template <> struct CodecOf<SU_ToolCall> {
    static Codec<SU_ToolCall> get() {
        return {
            [](const SU_ToolCall& v) -> Json { return to_json(v.toolCall); },
            [](const Json& j)        -> SU_ToolCall { return {from_json<ToolCall>(j)}; }};
    }
};
template <> struct CodecOf<SU_ToolCallUpdate> {
    static Codec<SU_ToolCallUpdate> get() {
        return {
            [](const SU_ToolCallUpdate& v) -> Json { return to_json(v.update); },
            [](const Json& j) -> SU_ToolCallUpdate { return {from_json<ToolCallUpdate>(j)}; }};
    }
};

template <> struct CodecOf<SU_AvailableCommands> {
    static Codec<SU_AvailableCommands> get() {
        return record<SU_AvailableCommands>(
            required("availableCommands", &SU_AvailableCommands::availableCommands));
    }
};
template <> struct CodecOf<SU_CurrentMode> {
    static Codec<SU_CurrentMode> get() {
        return record<SU_CurrentMode>(required("currentModeId", &SU_CurrentMode::currentModeId));
    }
};
template <> struct CodecOf<SU_ConfigOptions> {
    static Codec<SU_ConfigOptions> get() {
        return record<SU_ConfigOptions>(
            required("configOptions", &SU_ConfigOptions::configOptions));
    }
};
template <> struct CodecOf<SU_SessionInfo> {
    static Codec<SU_SessionInfo> get() {
        return record<SU_SessionInfo>(
            optional ("title",     &SU_SessionInfo::title),
            optional ("updatedAt", &SU_SessionInfo::updatedAt),
            meta("_meta",     &SU_SessionInfo::meta));
    }
};
template <> struct CodecOf<SU_Usage> {
    static Codec<SU_Usage> get() {
        return record<SU_Usage>(
            required("used", &SU_Usage::used),
            required("size", &SU_Usage::size),
            optional("cost", &SU_Usage::cost));
    }
};

//==============================================================================
//  SessionUpdate as a tagged sum.
//==============================================================================
template <> struct CodecOf<SessionUpdate> {
    static Codec<SessionUpdate> get() {
        return sum_tagged<SessionUpdate>("sessionUpdate",
            arm<SessionUpdate, SU_UserMessageChunk>   ("user_message_chunk"),
            arm<SessionUpdate, SU_AgentMessageChunk>  ("agent_message_chunk"),
            arm<SessionUpdate, SU_AgentThoughtChunk>  ("agent_thought_chunk"),
            arm<SessionUpdate, SU_Plan>               ("plan"),
            arm<SessionUpdate, SU_ToolCall>           ("tool_call"),
            arm<SessionUpdate, SU_ToolCallUpdate>     ("tool_call_update"),
            arm<SessionUpdate, SU_AvailableCommands>  ("available_commands_update"),
            arm<SessionUpdate, SU_CurrentMode>        ("current_mode_update"),
            arm<SessionUpdate, SU_ConfigOptions>      ("config_option_update"),
            arm<SessionUpdate, SU_SessionInfo>        ("session_info_update"),
            arm<SessionUpdate, SU_Usage>              ("usage_update"));
    }
};

} // namespace acp
