// SPDX-License-Identifier: Apache-2.0
//
// examples/echo_agent.cpp — a minimal but complete ACP agent over stdio.
//
//   What it does:
//     • Responds to `initialize` with its capabilities.
//     • On `session/new` returns a fresh sessionId.
//     • On `session/prompt`:
//         - streams an `agent_message_chunk` echoing the user's text
//         - sends a `plan` with a single item
//         - replies with stopReason = end_turn
//     • On `session/cancel` immediately answers any pending prompt as cancelled.
//
//   Run:    ./echo_agent
//   Talk:   pipe JSON-RPC line frames in on stdin; observe responses on stdout.
//
//   This is the "hello world" pattern for the agent side. Real agents would
//   call out to an LLM, manage tool calls, and request permission from the
//   client between calls.
//
#include <acp/acp.hpp>

#include <iostream>
#include <sstream>

using namespace acp;

// Extract the textual portion of a prompt for echo display.
static std::string flatten_text(const List<ContentBlock>& prompt) {
    std::ostringstream os;
    for (const auto& cb : prompt) {
        match(cb,
            [&](const TextContent& t)         { os << t.text; },
            [&](const ResourceContent&)       { os << "[resource]"; },
            [&](const ResourceLinkContent& l) { os << "[" << l.name << "]"; },
            [&](const ImageContent&)          { os << "[image]"; },
            [&](const AudioContent&)          { os << "[audio]"; });
    }
    return os.str();
}

int main() {
    AgentHandlers h;

    // The `ClientConnection` we set up below captures itself in handlers via a
    // shared_ptr so notifications can be sent during request handling.
    std::shared_ptr<ClientConnection> conn;

    h.on_initialize = [](const InitializeParams&) {
        InitializeResult r;
        r.agentCapabilities.loadSession                       = false;
        r.agentCapabilities.promptCapabilities.embeddedContext = true;
        r.agentInfo = Just<ImplementationInfo>({
            "acp-cpp-echo-agent",
            Just<std::string>("Echo Agent"),
            Just<std::string>(kLibraryVersion)});
        return r;
    };

    h.on_session_new = [](const NewSessionParams&) {
        return NewSessionResult{
            SessionId{std::string("sess_1")}, Nothing, Nothing, Json::object()};
    };

    h.on_session_prompt = [&](const PromptParams& p) {
        // Stream a plan, then a text reply.
        SessionUpdateMsg upd;
        upd.sessionId = p.sessionId;

        upd.update = SU_Plan{{
            PlanEntry{"Echo the prompt back", PlanEntryPriority::High,
                      PlanEntryStatus::InProgress}}};
        conn->session_update(upd);

        std::string echoed = std::string("you said: ") + flatten_text(p.prompt);
        upd.update = SU_AgentMessageChunk{
            TextContent{echoed, Nothing, Json::object()}, Nothing};
        conn->session_update(upd);

        upd.update = SU_Plan{{
            PlanEntry{"Echo the prompt back", PlanEntryPriority::High,
                      PlanEntryStatus::Completed}}};
        conn->session_update(upd);

        return PromptResult{StopReason::EndTurn};
    };

    h.on_session_cancel = [](const CancelParams&) {
        // Real agents would interrupt their model + tool calls here. The
        // outstanding session/prompt should resolve with stopReason=cancelled,
        // which means the in-flight handler needs to know about cancellation.
        // For the echo agent every prompt completes immediately so there's
        // nothing to do.
    };

    StdioTransport stdio(std::cin, std::cout);
    conn = std::make_shared<ClientConnection>(stdio.sink(), std::move(h));
    stdio.start(conn->engine());
    stdio.join();
    return 0;
}
