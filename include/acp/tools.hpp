// SPDX-License-Identifier: Apache-2.0
//
// acp/tools.hpp — tool calls, plans, permissions.
//
//   Denotationally:
//
//     ToolKind     ≅ Read | Edit | Delete | Move | Search
//                  | Execute | Think | Fetch | SwitchMode | Other
//     ToolCallStatus    ≅ Pending | InProgress | Completed | Failed
//     PlanEntryStatus   ≅ Pending | InProgress | Completed
//     PlanEntryPriority ≅ High | Medium | Low
//     PermOptKind  ≅ AllowOnce | AllowAlways | RejectOnce | RejectAlways
//
//     ToolCallContent ≅ Content ContentBlock
//                     + Diff   { path; oldText?; newText }
//                     + Terminal { terminalId }
//
//     ToolCallLocation ≅ { path; line? }
//
//     ToolCall  ≅ Π { toolCallId; title; kind; status;
//                     content : List ToolCallContent;
//                     locations : List ToolCallLocation;
//                     rawInput?, rawOutput?; meta }
//
//     ToolCallUpdate ≅ "all but toolCallId optional" variant of ToolCall.
//
//     RequestPermissionOutcome ≅ Cancelled | Selected { optionId }
//
#pragma once

#include <acp/content.hpp>

namespace acp {

//==============================================================================
//  ToolKind  (closed enum, string-tagged)
//==============================================================================
enum class ToolKind {
    Read, Edit, Delete, Move, Search, Execute, Think, Fetch, SwitchMode, Other
};
template <> struct CodecOf<ToolKind> {
    static Codec<ToolKind> get() {
        return enum_codec<ToolKind>(
            EnumMapping<ToolKind>{ToolKind::Read,       "read"},
            EnumMapping<ToolKind>{ToolKind::Edit,       "edit"},
            EnumMapping<ToolKind>{ToolKind::Delete,     "delete"},
            EnumMapping<ToolKind>{ToolKind::Move,       "move"},
            EnumMapping<ToolKind>{ToolKind::Search,     "search"},
            EnumMapping<ToolKind>{ToolKind::Execute,    "execute"},
            EnumMapping<ToolKind>{ToolKind::Think,      "think"},
            EnumMapping<ToolKind>{ToolKind::Fetch,      "fetch"},
            EnumMapping<ToolKind>{ToolKind::SwitchMode, "switch_mode"},
            EnumMapping<ToolKind>{ToolKind::Other,      "other"});
    }
};

//==============================================================================
//  ToolCallStatus
//==============================================================================
enum class ToolCallStatus { Pending, InProgress, Completed, Failed };
template <> struct CodecOf<ToolCallStatus> {
    static Codec<ToolCallStatus> get() {
        return enum_codec<ToolCallStatus>(
            EnumMapping<ToolCallStatus>{ToolCallStatus::Pending,    "pending"},
            EnumMapping<ToolCallStatus>{ToolCallStatus::InProgress, "in_progress"},
            EnumMapping<ToolCallStatus>{ToolCallStatus::Completed,  "completed"},
            EnumMapping<ToolCallStatus>{ToolCallStatus::Failed,     "failed"});
    }
};

//==============================================================================
//  ToolCallContent  ≅ Content + Diff + Terminal  (tag = "type")
//==============================================================================
struct TCC_Content   { ContentBlock content; };
struct TCC_Diff      {
    std::string path;
    Maybe<std::string> oldText;   // Nothing ⇒ new file
    std::string newText;
};
struct TCC_Terminal  { std::string terminalId; };

using ToolCallContent = Sum<TCC_Content, TCC_Diff, TCC_Terminal>;

template <> struct CodecOf<TCC_Content> {
    static Codec<TCC_Content> get() {
        return record<TCC_Content>(required("content", &TCC_Content::content));
    }
};
template <> struct CodecOf<TCC_Diff> {
    static Codec<TCC_Diff> get() {
        return record<TCC_Diff>(
            required ("path",    &TCC_Diff::path),
            optional ("oldText", &TCC_Diff::oldText),
            required ("newText", &TCC_Diff::newText));
    }
};
template <> struct CodecOf<TCC_Terminal> {
    static Codec<TCC_Terminal> get() {
        return record<TCC_Terminal>(required("terminalId", &TCC_Terminal::terminalId));
    }
};
template <> struct CodecOf<ToolCallContent> {
    static Codec<ToolCallContent> get() {
        return sum_tagged<ToolCallContent>("type",
            arm<ToolCallContent, TCC_Content> ("content"),
            arm<ToolCallContent, TCC_Diff>    ("diff"),
            arm<ToolCallContent, TCC_Terminal>("terminal"));
    }
};

//==============================================================================
//  ToolCallLocation — { path; line? }
//==============================================================================
struct ToolCallLocation {
    std::string path;
    Maybe<std::int64_t> line;
};
template <> struct CodecOf<ToolCallLocation> {
    static Codec<ToolCallLocation> get() {
        return record<ToolCallLocation>(
            required ("path", &ToolCallLocation::path),
            optional ("line", &ToolCallLocation::line));
    }
};

//==============================================================================
//  ToolCall — the initial announcement of a tool invocation.
//==============================================================================
struct ToolCall {
    ToolCallId toolCallId;
    std::string title;
    ToolKind       kind   = ToolKind::Other;
    ToolCallStatus status = ToolCallStatus::Pending;
    List<ToolCallContent>  content;
    List<ToolCallLocation> locations;
    Maybe<Json> rawInput;
    Maybe<Json> rawOutput;
    Json meta = Json::object();
};
template <> struct CodecOf<ToolCall> {
    static Codec<ToolCall> get() {
        return record<ToolCall>(
            required ("toolCallId", &ToolCall::toolCallId),
            required ("title",      &ToolCall::title),
            defaulted("kind",       &ToolCall::kind,      ToolKind::Other),
            defaulted("status",     &ToolCall::status,    ToolCallStatus::Pending),
            defaulted("content",    &ToolCall::content,   List<ToolCallContent>{}),
            defaulted("locations",  &ToolCall::locations, List<ToolCallLocation>{}),
            optional ("rawInput",   &ToolCall::rawInput),
            optional ("rawOutput",  &ToolCall::rawOutput),
            meta("_meta",      &ToolCall::meta));
    }
};

//==============================================================================
//  ToolCallUpdate — a delta: every field except toolCallId is optional, meaning
//                  "leave unchanged".
//==============================================================================
struct ToolCallUpdate {
    ToolCallId toolCallId;
    Maybe<std::string>      title;
    Maybe<ToolKind>         kind;
    Maybe<ToolCallStatus>   status;
    Maybe<List<ToolCallContent>>  content;
    Maybe<List<ToolCallLocation>> locations;
    Maybe<Json> rawInput;
    Maybe<Json> rawOutput;
    Json meta = Json::object();
};
template <> struct CodecOf<ToolCallUpdate> {
    static Codec<ToolCallUpdate> get() {
        return record<ToolCallUpdate>(
            required ("toolCallId", &ToolCallUpdate::toolCallId),
            optional ("title",      &ToolCallUpdate::title),
            optional ("kind",       &ToolCallUpdate::kind),
            optional ("status",     &ToolCallUpdate::status),
            optional ("content",    &ToolCallUpdate::content),
            optional ("locations",  &ToolCallUpdate::locations),
            optional ("rawInput",   &ToolCallUpdate::rawInput),
            optional ("rawOutput",  &ToolCallUpdate::rawOutput),
            meta("_meta",      &ToolCallUpdate::meta));
    }
};

//==============================================================================
//  Plans
//==============================================================================
enum class PlanEntryPriority { High, Medium, Low };
enum class PlanEntryStatus   { Pending, InProgress, Completed };

template <> struct CodecOf<PlanEntryPriority> {
    static Codec<PlanEntryPriority> get() {
        return enum_codec<PlanEntryPriority>(
            EnumMapping<PlanEntryPriority>{PlanEntryPriority::High,   "high"},
            EnumMapping<PlanEntryPriority>{PlanEntryPriority::Medium, "medium"},
            EnumMapping<PlanEntryPriority>{PlanEntryPriority::Low,    "low"});
    }
};
template <> struct CodecOf<PlanEntryStatus> {
    static Codec<PlanEntryStatus> get() {
        return enum_codec<PlanEntryStatus>(
            EnumMapping<PlanEntryStatus>{PlanEntryStatus::Pending,    "pending"},
            EnumMapping<PlanEntryStatus>{PlanEntryStatus::InProgress, "in_progress"},
            EnumMapping<PlanEntryStatus>{PlanEntryStatus::Completed,  "completed"});
    }
};

struct PlanEntry {
    std::string content;
    PlanEntryPriority priority = PlanEntryPriority::Medium;
    PlanEntryStatus   status   = PlanEntryStatus::Pending;
};
template <> struct CodecOf<PlanEntry> {
    static Codec<PlanEntry> get() {
        return record<PlanEntry>(
            required ("content",  &PlanEntry::content),
            defaulted("priority", &PlanEntry::priority, PlanEntryPriority::Medium),
            defaulted("status",   &PlanEntry::status,   PlanEntryStatus::Pending));
    }
};

//==============================================================================
//  Permission options & outcomes
//==============================================================================
enum class PermissionOptionKind { AllowOnce, AllowAlways, RejectOnce, RejectAlways };
template <> struct CodecOf<PermissionOptionKind> {
    static Codec<PermissionOptionKind> get() {
        return enum_codec<PermissionOptionKind>(
            EnumMapping<PermissionOptionKind>{PermissionOptionKind::AllowOnce,    "allow_once"},
            EnumMapping<PermissionOptionKind>{PermissionOptionKind::AllowAlways,  "allow_always"},
            EnumMapping<PermissionOptionKind>{PermissionOptionKind::RejectOnce,   "reject_once"},
            EnumMapping<PermissionOptionKind>{PermissionOptionKind::RejectAlways, "reject_always"});
    }
};

struct PermissionOption {
    std::string optionId;
    std::string name;
    PermissionOptionKind kind = PermissionOptionKind::AllowOnce;
};
template <> struct CodecOf<PermissionOption> {
    static Codec<PermissionOption> get() {
        return record<PermissionOption>(
            required ("optionId", &PermissionOption::optionId),
            required ("name",     &PermissionOption::name),
            defaulted("kind",     &PermissionOption::kind, PermissionOptionKind::AllowOnce));
    }
};

// Outcome is itself a tagged sum (key = "outcome"):
//
//     { "outcome": "cancelled" }
//     { "outcome": "selected", "optionId": "..." }
//
struct PO_Cancelled {};
struct PO_Selected  { std::string optionId; };
using RequestPermissionOutcome = Sum<PO_Cancelled, PO_Selected>;

template <> struct CodecOf<PO_Cancelled> {
    static Codec<PO_Cancelled> get() {
        return {[](const PO_Cancelled&) -> Json { return Json::object(); },
                [](const Json&)         -> PO_Cancelled { return {}; }};
    }
};
template <> struct CodecOf<PO_Selected> {
    static Codec<PO_Selected> get() {
        return record<PO_Selected>(required("optionId", &PO_Selected::optionId));
    }
};
template <> struct CodecOf<RequestPermissionOutcome> {
    static Codec<RequestPermissionOutcome> get() {
        return sum_tagged<RequestPermissionOutcome>("outcome",
            arm<RequestPermissionOutcome, PO_Cancelled>("cancelled"),
            arm<RequestPermissionOutcome, PO_Selected> ("selected"));
    }
};

} // namespace acp
