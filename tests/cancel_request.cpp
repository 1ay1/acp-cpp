// SPDX-License-Identifier: Apache-2.0
//
// cancel_request.cpp — verifies the generic $/cancel_request notification
// (stabilized 2026-06-29) end to end:
//
//   1. Peer→agent: an AgentHandlers.on_cancel_request handler fires with the
//      raw JSON-RPC id the client asked to cancel.
//   2. Engine self-service: when a peer cancels an id that names an OUTBOUND
//      request WE are awaiting, that future fails with errc::Cancelled instead
//      of hanging until the deadline.
//
#include <acp/acp.hpp>

#include <atomic>
#include "agtest.hpp"
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
        cv_.wait(lk, [&] { return !q_.empty() || closed_; });
        if (q_.empty()) { closed = closed_; return false; }
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }
    void close() { { std::lock_guard lk(mu_); closed_ = true; } cv_.notify_all(); }
private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::string> q_;
    bool closed_ = false;
};

} // namespace

using namespace acp;

TEST_CASE("cancel_request") {
    Mailbox client_to_agent, agent_to_client;

    // ---- AGENT side: record the cancelled id via on_cancel_request ---------
    std::atomic<bool> cancel_seen{false};
    std::string       cancelled_id_dump;
    std::mutex        cid_mu;

    AgentHandlers ah;
    ah.on_initialize = [](const InitializeParams&) { return InitializeResult{}; };
    ah.on_cancel_request = [&](const RpcId& id) {
        { std::lock_guard lk(cid_mu); cancelled_id_dump = id.dump(); }
        cancel_seen.store(true);
    };
    ClientConnection agent_side(
        [&](std::string_view l) { agent_to_client.push(std::string(l)); }, std::move(ah));

    // ---- CLIENT side -------------------------------------------------------
    ClientHandlers ch;
    AgentConnection client(
        [&](std::string_view l) { client_to_agent.push(std::string(l)); }, std::move(ch));

    // ---- pumps -------------------------------------------------------------
    std::atomic<bool> alive{true};
    std::vector<std::thread> pumps;
    auto pump = [&](Mailbox& mb, RpcEngine& dst) {
        pumps.emplace_back([&] {
            std::string line; bool closed = false;
            while (alive.load()) {
                if (!mb.pop(line, closed)) { if (closed) return; continue; }
                dst.feed_line(line);
            }
        });
    };
    pump(client_to_agent, agent_side.engine());
    pump(agent_to_client, client.engine());

    // ======================================================================
    // 1) Client → agent: cancel an arbitrary (peer-chosen) request id.
    // ======================================================================
    client.engine().notify_cancel_request(RpcId(std::int64_t{99}));
    for (int i = 0; i < 200 && !cancel_seen.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(cancel_seen.load() && "on_cancel_request never fired");
    {
        std::lock_guard lk(cid_mu);
        assert(cancelled_id_dump == "99" && "wrong cancelled id delivered");
    }

    // ======================================================================
    // 2) Engine self-service: the AGENT makes an outbound request to the
    //    client and waits on it; the client never answers but instead sends
    //    $/cancel_request for that exact id. The agent's future must fail with
    //    errc::Cancelled promptly (not hang to the deadline).
    //
    //    We install a client handler for the outbound method that, instead of
    //    replying, echoes a cancel for the request's own id.
    // ======================================================================
    client.engine().on_notification("session/update", [](const Json&) {});
    // Register a raw request handler on the client that swallows the request
    // (returns Nothing, i.e. defers) and cancels it by id.
    client.engine().on_request("_test/slow",
        [&](const RpcId& id, const Json&) -> Maybe<Json> {
            client.engine().notify_cancel_request(id);   // cancel by our own id
            return Nothing;                              // never actually reply
        });

    auto fut = agent_side.engine().ext_request("_test/slow", Json::object());
    bool threw_cancelled = false;
    try {
        fut.get();
    } catch (const RpcError& e) {
        threw_cancelled = (e.code == errc::Cancelled);
    }
    assert(threw_cancelled && "outbound request was not cancelled by $/cancel_request");

    // ---- teardown ----------------------------------------------------------
    alive.store(false);
    client_to_agent.close();
    agent_to_client.close();
    for (auto& t : pumps) if (t.joinable()) t.join();

    std::cout << "cancel_request OK\n";
}
