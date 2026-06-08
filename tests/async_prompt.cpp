// SPDX-License-Identifier: Apache-2.0
//
// async_prompt.cpp — proves the deferred-response path:
//
//   The agent registers `on_session_prompt_async`. Its handler does NOT
//   answer inline; it hands the Responder to a worker thread. The worker
//   then calls BACK to the client via session/request_permission (an outbound
//   request whose response must be read by the same engine) BEFORE resolving
//   the prompt. A synchronous handler that blocked the reader thread here
//   would deadlock; the async path keeps the reader free, so both the
//   permission round-trip and the eventual prompt reply complete.
//
#include <acp/acp.hpp>

#include <cassert>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

class Mailbox {
public:
    void push(std::string s) {
        { std::lock_guard lk(mu_); q_.push_back(std::move(s)); }
        cv_.notify_one();
    }
    bool pop(std::string& out, bool& closed) {
        std::unique_lock lk(mu_);
        cv_.wait(lk, [&]{ return !q_.empty() || closed_; });
        if (q_.empty()) { closed = closed_; return false; }
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }
    void close() {
        { std::lock_guard lk(mu_); closed_ = true; }
        cv_.notify_all();
    }
private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::string> q_;
    bool closed_ = false;
};

} // namespace

using namespace acp;

int main() {
    Mailbox client_to_agent, agent_to_client;

    std::shared_ptr<ClientConnection> agent_side;
    std::atomic<bool> worker_done{false};

    AgentHandlers a;
    a.on_initialize = [](const InitializeParams&) {
        InitializeResult r;
        r.agentCapabilities.promptCapabilities.embeddedContext = true;
        return r;
    };
    a.on_session_new = [](const NewSessionParams&) {
        return NewSessionResult{SessionId{std::string("s1")}, Nothing, Nothing, Json::object()};
    };
    // The deferred prompt: spawn a worker that calls request_permission, waits
    // for the client's answer, then resolves the prompt.
    a.on_session_prompt_async =
        [&](const PromptParams& p, RpcEngine::Responder<PromptResult> resp) {
            std::thread([&, sid = p.sessionId, r = std::move(resp)]() mutable {
                RequestPermissionParams rp;
                rp.sessionId = sid;
                rp.toolCall.toolCallId = ToolCallId{std::string("tc1")};
                rp.options.push_back(
                    PermissionOption{"allow", "Allow", PermissionOptionKind::AllowOnce, Json::object()});
                // This blocks the WORKER thread, not the reader thread.
                auto outcome = agent_side->request_permission(rp).get();
                bool allowed = std::holds_alternative<PO_Selected>(outcome.outcome);

                SessionUpdateMsg upd;
                upd.sessionId = sid;
                upd.update = SU_AgentMessageChunk{
                    TextContent{allowed ? "did it" : "denied", Nothing, Json::object()}, Nothing};
                agent_side->session_update(upd);

                worker_done.store(true);
                r.ok(PromptResult{StopReason::EndTurn});
            }).detach();
        };
    a.on_session_cancel = [](const CancelParams&) {};

    agent_side = std::make_shared<ClientConnection>(
        [&](std::string_view l){ agent_to_client.push(std::string(l)); }, std::move(a));

    // Client side: answers the permission request by selecting "allow".
    ClientHandlers c;
    std::atomic<int> updates{0};
    c.on_session_update = [&](const SessionUpdateMsg&) { ++updates; };
    c.on_request_permission = [](const RequestPermissionParams& p) {
        return RequestPermissionResult{
            RequestPermissionOutcome{PO_Selected{p.options.front().optionId, Json::object()}},
            Json::object()};
    };

    AgentConnection agent([&](std::string_view l){ client_to_agent.push(std::string(l)); },
                          std::move(c));

    std::atomic<bool> alive{true};
    auto pump = [&](Mailbox& mb, RpcEngine& dst) {
        std::thread([&]{
            std::string line; bool closed = false;
            while (alive.load()) {
                if (!mb.pop(line, closed)) { if (closed) return; continue; }
                dst.feed_line(line);
            }
        }).detach();
    };
    pump(client_to_agent, agent_side->engine());
    pump(agent_to_client, agent.engine());

    agent.initialize(InitializeParams{}).get();
    auto ns = agent.session_new(NewSessionParams{".", {}, Nothing}).get();

    PromptParams pp;
    pp.sessionId = ns.sessionId;
    pp.prompt.push_back(TextContent{"go", Nothing, Json::object()});
    auto pr = agent.session_prompt(pp).get();   // resolves only after the round-trip
    assert(pr.stopReason == StopReason::EndTurn);
    assert(worker_done.load());

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(updates.load() == 1);   // the "did it" chunk made it through

    alive.store(false);
    client_to_agent.close();
    agent_to_client.close();
    std::cout << "async_prompt deferred-response OK\n";
    return 0;
}
