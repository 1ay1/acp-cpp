// SPDX-License-Identifier: Apache-2.0
//
// In-memory loopback integration test: a ClientConnection (agent role) and an
// AgentConnection (client role) are wired by a pair of queues. We run a real
// `initialize` and `session/new` and `session/prompt` round-trip, plus an
// `fs/read_text_file` callback from the agent to the client.
//
#include <acp/acp.hpp>

#include <cassert>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

// A thread-safe in-memory queue of newline-delimited JSON-RPC envelopes.
class Mailbox {
public:
    void push(std::string s) {
        {
            std::lock_guard lk(mu_);
            q_.push_back(std::move(s));
        }
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
    Mailbox client_to_agent;   // client writes here, agent reads
    Mailbox agent_to_client;   // agent writes here, client reads

    // -------- Build the AGENT side (a ClientConnection) ---------------------
    AgentHandlers a_handlers;

    a_handlers.on_initialize = [](const InitializeParams& p) {
        InitializeResult r;
        assert(p.protocolVersion == kProtocolVersion);
        assert(p.clientCapabilities.fs.readTextFile);
        r.agentCapabilities.loadSession = true;
        r.agentCapabilities.promptCapabilities.embeddedContext = true;
        r.agentInfo = Just<ImplementationInfo>({"loopback-agent",
                                                Just<std::string>("Loopback Agent"),
                                                Just<std::string>(kLibraryVersion)});
        return r;
    };
    a_handlers.on_session_new = [](const NewSessionParams& p) {
        assert(!p.cwd.empty());
        return NewSessionResult{SessionId{std::string("sess_test")}, Nothing, Nothing, Json::object()};
    };
    a_handlers.on_session_prompt = [](const PromptParams& p) {
        assert(p.sessionId == SessionId{std::string("sess_test")});
        assert(!p.prompt.empty());
        return PromptResult{StopReason::EndTurn};
    };
    a_handlers.on_session_cancel = [](const CancelParams&) { /* no-op */ };

    ClientConnection agent_side([&](std::string_view line){ agent_to_client.push(std::string(line)); },
                                 std::move(a_handlers));
    // Agent's `request_permission` could call the client during prompt; for this
    // test the prompt handler returns immediately.

    // -------- Build the CLIENT side (an AgentConnection) --------------------
    ClientHandlers c_handlers;

    // The agent might call fs/read_text_file during the prompt. We won't drive it
    // in this test but install the handler so the surface is exercised.
    c_handlers.on_fs_read_text_file = [](const ReadTextFileParams&) {
        return ReadTextFileResult{"// dummy file contents\n"};
    };

    // We also accept session/update notifications and just count them.
    std::atomic<int> updates_seen{0};
    c_handlers.on_session_update = [&](const SessionUpdateMsg&) {
        ++updates_seen;
    };

    AgentConnection agent([&](std::string_view line){ client_to_agent.push(std::string(line)); },
                          std::move(c_handlers));

    // -------- Pump threads --------------------------------------------------
    std::atomic<bool> alive{true};
    auto pump = [&](Mailbox& mb, RpcEngine& dst) {
        std::thread([&]{
            while (alive.load()) {
                std::string line;
                bool closed = false;
                if (!mb.pop(line, closed)) {
                    if (closed) return;
                    continue;
                }
                dst.feed_line(line);
            }
        }).detach();
    };
    pump(client_to_agent, agent_side.engine());
    pump(agent_to_client, agent.engine());

    // -------- Drive the protocol -------------------------------------------
    InitializeParams ip;
    ip.clientCapabilities.fs.readTextFile = true;
    ip.clientCapabilities.fs.writeTextFile = true;
    ip.clientCapabilities.terminal = true;
    ip.clientInfo = Just<ImplementationInfo>({"loopback-client", Nothing, Nothing});

    auto fut_init = agent.initialize(ip);
    auto init_result = fut_init.get();
    assert(init_result.protocolVersion == kProtocolVersion);
    assert(init_result.agentCapabilities.loadSession == true);
    assert(init_result.agentInfo.has_value() && init_result.agentInfo->name == "loopback-agent");

    NewSessionParams nsp;
    nsp.cwd = "/tmp/proj";
    auto fut_new = agent.session_new(nsp);
    auto ns = fut_new.get();
    assert(ns.sessionId == SessionId{std::string("sess_test")});

    // Send a session/update notification from the agent side, observe it on the
    // client side.
    SessionUpdateMsg upd;
    upd.sessionId = ns.sessionId;
    upd.update = SU_AgentMessageChunk{TextContent{"hello there", Nothing, Json::object()}, Nothing};
    agent_side.session_update(upd);

    // Now do a prompt.
    PromptParams pp;
    pp.sessionId = ns.sessionId;
    pp.prompt.push_back(TextContent{"Hi!", Nothing, Json::object()});
    auto fut_p = agent.session_prompt(pp);
    auto pr = fut_p.get();
    assert(pr.stopReason == StopReason::EndTurn);

    // Give the notification pump a beat to deliver the update.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(updates_seen.load() == 1);

    // Send a cancel notification (no response expected).
    agent.session_cancel(CancelParams{ns.sessionId});
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Tear down.
    alive.store(false);
    client_to_agent.close();
    agent_to_client.close();

    std::cout << "loopback integration OK\n";
    return 0;
}
