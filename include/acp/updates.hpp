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
//      + Notice               { severity; title; description? }         (v1.11)
//      + CompactionUpdate     { compactionId; status; summary?; error? } (v1.11)
//      + CompactionSummaryChunk { compactionId; content }               (v1.11)
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

struct SU_Plan               { List<PlanEntry> entries; Json meta = Json::object(); };

struct SU_ToolCall           { ToolCall       toolCall; };
struct SU_ToolCallUpdate     { ToolCallUpdate update;   };

struct SU_AvailableCommands  { List<AvailableCommand> availableCommands; Json meta = Json::object(); };
struct SU_CurrentMode        { SessionModeId currentModeId; Json meta = Json::object(); };
struct SU_ConfigOptions      { List<ConfigOption> configOptions; Json meta = Json::object(); };

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

// v1.11. Only sent when the client advertised clientCapabilities.session.notices.
// severity is an open enum: info | warning | error | _custom.
struct SU_Notice {
    std::string severity = "info";
    std::string title;                 // non-empty plain text
    Maybe<std::string> description;
    Json meta = Json::object();
};

// v1.11. Only sent when the client advertised clientCapabilities.session.compaction.
// status is an open enum: in_progress | completed | failed | cancelled.
// summary, when present, replaces whatever summary was streamed so far.
struct SU_Compaction {
    std::string compactionId;
    std::string status = "in_progress";
    Maybe<List<ContentBlock>> summary;
    Maybe<std::string> error;
    Json meta = Json::object();
};
struct SU_CompactionSummaryChunk {
    std::string compactionId;
    ContentBlock content;
    Json meta = Json::object();
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
    SU_Usage,
    SU_Notice,
    SU_Compaction,
    SU_CompactionSummaryChunk>;

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
        return record<SU_Plan>(
            required("entries", &SU_Plan::entries),
            meta("_meta",  &SU_Plan::meta));
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
            required("availableCommands", &SU_AvailableCommands::availableCommands),
            meta("_meta",            &SU_AvailableCommands::meta));
    }
};
template <> struct CodecOf<SU_CurrentMode> {
    static Codec<SU_CurrentMode> get() {
        return record<SU_CurrentMode>(
            required("currentModeId", &SU_CurrentMode::currentModeId),
            meta("_meta",        &SU_CurrentMode::meta));
    }
};
template <> struct CodecOf<SU_ConfigOptions> {
    static Codec<SU_ConfigOptions> get() {
        return record<SU_ConfigOptions>(
            required("configOptions", &SU_ConfigOptions::configOptions),
            meta("_meta",        &SU_ConfigOptions::meta));
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
template <> struct CodecOf<SU_Notice> {
    static Codec<SU_Notice> get() {
        return record<SU_Notice>(
            required("severity",    &SU_Notice::severity),
            required("title",       &SU_Notice::title),
            optional("description", &SU_Notice::description),
            meta("_meta",           &SU_Notice::meta));
    }
};
template <> struct CodecOf<SU_Compaction> {
    static Codec<SU_Compaction> get() {
        return record<SU_Compaction>(
            required("compactionId", &SU_Compaction::compactionId),
            required("status",       &SU_Compaction::status),
            optional("summary",      &SU_Compaction::summary),
            optional("error",        &SU_Compaction::error),
            meta("_meta",            &SU_Compaction::meta));
    }
};
template <> struct CodecOf<SU_CompactionSummaryChunk> {
    static Codec<SU_CompactionSummaryChunk> get() {
        return record<SU_CompactionSummaryChunk>(
            required("compactionId", &SU_CompactionSummaryChunk::compactionId),
            required("content",      &SU_CompactionSummaryChunk::content),
            meta("_meta",            &SU_CompactionSummaryChunk::meta));
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
            arm<SessionUpdate, SU_Usage>              ("usage_update"),
            arm<SessionUpdate, SU_Notice>             ("notice"),
            arm<SessionUpdate, SU_Compaction>         ("compaction_update"),
            arm<SessionUpdate, SU_CompactionSummaryChunk>("compaction_summary_chunk"));
    }
};

} // namespace acp
