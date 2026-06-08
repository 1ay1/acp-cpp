// SPDX-License-Identifier: Apache-2.0
// All-headers compile + round-trip smoke for the spec types.
#include <acp/methods.hpp>
#include <acp/updates.hpp>

#include <cassert>
#include <iostream>

using namespace acp;

template <class T>
static void round_trip(const T& v, const char* label) {
    Json j = to_json(v);
    T back = from_json<T>(j);
    Json j2 = to_json(back);
    if (j != j2) {
        std::cerr << "round-trip differs for " << label << "\n  before: "
                  << j.dump(2) << "\n  after:  " << j2.dump(2) << "\n";
        std::abort();
    }
}

int main() {
    // initialize
    {
        InitializeParams p;
        p.protocolVersion = kProtocolVersion;
        p.clientCapabilities.fs.readTextFile = true;
        p.clientCapabilities.fs.writeTextFile = true;
        p.clientCapabilities.terminal = true;
        p.clientInfo = Just<ImplementationInfo>({"my-client", Just<std::string>("My Client"),
                                                  Just<std::string>("1.0.0")});
        round_trip(p, "InitializeParams");

        InitializeResult r;
        r.agentCapabilities.loadSession = true;
        r.agentCapabilities.promptCapabilities.image = true;
        r.agentCapabilities.mcpCapabilities.http = true;
        r.agentCapabilities.sessionCapabilities.resume = Just(Unit{});
        r.agentCapabilities.sessionCapabilities.deleteCap = Just(Unit{});
        r.authMethods.push_back({"a-login", "Agent login", Just<std::string>("Sign in"), "agent"});
        round_trip(r, "InitializeResult");
    }

    // session/new with mixed MCP transports
    {
        NewSessionParams p;
        p.cwd = "/home/user/project";
        p.mcpServers.push_back(StdioMcpServer{"fs", "/usr/local/bin/mcp-fs", {"--stdio"}, {}});
        p.mcpServers.push_back(HttpMcpServer {"api", "https://api.example/mcp",
                                              {KeyValue{"Authorization", "Bearer X"}}});
        p.additionalDirectories = Just<List<std::string>>({"/shared/lib"});
        round_trip(p, "NewSessionParams");
    }

    // session/prompt
    {
        PromptParams p;
        p.sessionId = SessionId{std::string("sess_x")};
        p.prompt.push_back(TextContent{"Hello", Nothing, Json::object()});
        round_trip(p, "PromptParams");

        PromptResult r; r.stopReason = StopReason::Cancelled;
        round_trip(r, "PromptResult");
    }

    // session/update — every arm
    {
        SessionUpdateMsg m; m.sessionId = SessionId{std::string("s")};
        // 1. agent_message_chunk
        m.update = SU_AgentMessageChunk{TextContent{"hi", Nothing, Json::object()}, Nothing};
        round_trip(m, "SessionUpdate.agent_message_chunk");
        // 2. plan
        m.update = SU_Plan{{PlanEntry{"step", PlanEntryPriority::High, PlanEntryStatus::Pending}}};
        round_trip(m, "SessionUpdate.plan");
        // 3. tool_call (flattened)
        ToolCall tc;
        tc.toolCallId = ToolCallId{std::string("call_1")};
        tc.title = "Read x";
        tc.kind = ToolKind::Read;
        tc.status = ToolCallStatus::Pending;
        m.update = SU_ToolCall{tc};
        Json j = to_json(m);
        assert(j["update"]["sessionUpdate"] == "tool_call");
        assert(j["update"]["toolCallId"] == "call_1");
        round_trip(m, "SessionUpdate.tool_call");
        // 4. tool_call_update
        ToolCallUpdate tcu;
        tcu.toolCallId = ToolCallId{std::string("call_1")};
        tcu.status = Just(ToolCallStatus::InProgress);
        m.update = SU_ToolCallUpdate{tcu};
        round_trip(m, "SessionUpdate.tool_call_update");
        // 5. usage_update
        m.update = SU_Usage{53000, 200000, Just<Cost>({0.045, "USD"})};
        round_trip(m, "SessionUpdate.usage_update");
    }

    // permissions
    {
        RequestPermissionParams p;
        p.sessionId = SessionId{std::string("s")};
        p.toolCall.toolCallId = ToolCallId{std::string("call_1")};
        p.options.push_back({"allow", "Allow once",  PermissionOptionKind::AllowOnce});
        p.options.push_back({"deny",  "Reject once", PermissionOptionKind::RejectOnce});
        round_trip(p, "RequestPermissionParams");

        RequestPermissionResult r1; r1.outcome = PO_Selected{"allow"};
        round_trip(r1, "RequestPermissionResult.selected");
        RequestPermissionResult r2; r2.outcome = PO_Cancelled{};
        round_trip(r2, "RequestPermissionResult.cancelled");
    }

    // fs + terminal
    {
        ReadTextFileParams rp{SessionId{std::string("s")}, "/x", Just<std::int64_t>(1), Just<std::int64_t>(50)};
        round_trip(rp, "ReadTextFileParams");
        WriteTextFileParams wp{SessionId{std::string("s")}, "/x", "hello"};
        round_trip(wp, "WriteTextFileParams");

        CreateTerminalParams cp;
        cp.sessionId = SessionId{std::string("s")};
        cp.command = "npm"; cp.args = {"test"};
        cp.env = {KeyValue{"NODE_ENV","test"}};
        cp.outputByteLimit = Just<std::int64_t>(1<<20);
        round_trip(cp, "CreateTerminalParams");

        TerminalOutputResult tr{"out\n", false, Just<TerminalExitStatus>({Just<std::int64_t>(0), Nothing})};
        round_trip(tr, "TerminalOutputResult");
    }

    // -----  v0.2.0 additions: spec-coverage drift fixes  -----

    // current_mode_update uses field name `currentModeId` on the wire.
    {
        SessionUpdateMsg m;
        m.sessionId = SessionId{std::string("s")};
        m.update = SU_CurrentMode{SessionModeId{std::string("focus")}};
        Json j = to_json(m);
        assert(j["update"]["currentModeId"] == "focus");
        round_trip(m, "SessionUpdate.current_mode_update");
    }

    // Annotations is now a real record (audience, lastModified, priority).
    {
        TextContent t;
        t.text = "hi";
        Annotations ann;
        ann.audience  = Just<List<Role>>({Role::Assistant, Role::User});
        ann.priority  = Just<double>(0.9);
        ann.lastModified = Just<std::string>("2025-01-01T00:00:00Z");
        t.annotations = Just(ann);
        ContentBlock cb = t;
        round_trip(cb, "ContentBlock.text+annotations");
        Json j = to_json(cb);
        assert(j["annotations"]["audience"][0] == "assistant");
        assert(j["annotations"]["priority"]    == 0.9);
    }

    // ConfigOption.options now models the SessionConfigSelectOptions sum.
    {
        // Ungrouped
        ConfigOption co;
        co.id = "model"; co.name = "Model"; co.category = Just<std::string>("model");
        co.currentValue = "gpt-5";
        co.options = ConfigSelectOptions{CSO_Ungrouped{{
            {"gpt-5", "GPT-5", Nothing, Json::object()},
            {"o4-mini", "o4-mini", Just<std::string>("smaller/cheap"), Json::object()}}}};
        round_trip(co, "ConfigOption.ungrouped");
        // Grouped
        ConfigOption cg;
        cg.id = "model"; cg.name = "Model"; cg.currentValue = "gpt-5";
        cg.options = ConfigSelectOptions{CSO_Grouped{{
            {"openai", "OpenAI", {{"gpt-5","GPT-5",Nothing,Json::object()}}, Json::object()},
            {"anthropic", "Anthropic", {{"claude-4","Claude 4",Nothing,Json::object()}}, Json::object()}}}};
        round_trip(cg, "ConfigOption.grouped");
    }

    // AvailableCommandInput is now a forward-compatible tagged sum.
    {
        AvailableCommand c;
        c.name = "diff"; c.description = "Show a diff";
        c.input = Just(AvailableCommandInput{UnstructuredCommandInput{"<path>", Json::object()}});
        round_trip(c, "AvailableCommand+unstructured-input");
    }

    std::cout << "all spec round-trips OK\n";
    return 0;
}
