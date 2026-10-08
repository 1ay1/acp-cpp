// tests/protocol_loopback.cpp — a client and an agent, two engines, no threads.
//
// The whole of ACP between two peers, driven by hand: one engine per side,
// frames carried from one side's Effects.send into the other's Received. If
// the library ever needs a thread, this test (built without -pthread) fails
// to link.

#include <acp/acp.hpp>

#include "agtest.hpp"

#include <deque>
#include <string>

using namespace acp;

namespace {

// The agent: answers initialize and session/new, defers session/prompt (it
// streams updates first, then replies), and asks the client for permission.
struct Agent {
    Engine      engine;
    Router<Agent> router;
    int         sessions = 0;
    Maybe<Id>   prompt_call;   // the deferred prompt, answered later

    Agent() {
        router.on<to_agent::Initialize>([](Agent&, const InitializeParams& p) {
            InitializeResult r;
            r.protocolVersion = p.protocolVersion;
            return r;
        });
        router.on<to_agent::SessionNew>([](Agent& a, const NewSessionParams&) {
            NewSessionResult r;
            r.sessionId = SessionId{"s" + std::to_string(++a.sessions)};
            return r;
        });
        router.defer<to_agent::SessionPrompt>();
    }
};

// The client: handles permission requests and collects session updates.
struct Client {
    Engine         engine;
    Router<Client> router;
    List<SessionUpdateMsg> updates;
    int            permissions_asked = 0;

    Client() {
        router.on<to_client::RequestPermission>([](Client& c, const RequestPermissionParams& p) {
            ++c.permissions_asked;
            RequestPermissionResult r;
            r.outcome = PO_Selected{p.options.front().optionId, Json::object()};
            return r;
        });
        router.on<to_client::SessionUpdate>([](Client& c, const SessionUpdateMsg& m) {
            c.updates.push_back(m);
        });
    }
};

// Deliver every frame in `wire` to `peer`, handling its calls with its router,
// and return what it sends back plus its completions.
template <class Peer>
Effects deliver(Peer& peer, std::deque<std::string>& wire, List<std::string>& back) {
    Effects all;
    while (!wire.empty()) {
        auto fx = step(peer.engine, Received{wire.front()});
        wire.pop_front();
        for (auto& call : fx.calls)
            if (auto frame = peer.router.handle(peer, call)) back.push_back(*frame);
        for (auto& note : fx.notifications) peer.router.handle(peer, note);
        for (auto& f : fx.send) back.push_back(f);
        all += std::move(fx);
    }
    return all;
}

}  // namespace

TEST_CASE("protocol loopback: a client and an agent, no threads") {
    Client client;
    Agent  agent;
    std::deque<std::string> to_agent_wire, to_client_wire;
    auto send = [](std::deque<std::string>& wire, const List<std::string>& frames) {
        for (const auto& f : frames) wire.push_back(f);
    };

    // initialize
    InitializeParams ip;
    ip.protocolVersion = kProtocolVersion;
    auto [init_id, init_fx] = request<to_agent::Initialize>(client.engine, ip);
    send(to_agent_wire, init_fx.send);
    List<std::string> back;
    deliver(agent, to_agent_wire, back);
    send(to_client_wire, back); back.clear();
    auto done = deliver(client, to_client_wire, back);
    CHECK(done.completed.size() == 1);
    CHECK(done.completed[0].id == init_id);
    auto init = result<to_agent::Initialize>(done.completed[0]);
    CHECK(init.has_value());
    CHECK(init->protocolVersion == kProtocolVersion);

    // session/new
    NewSessionParams np;
    np.cwd = "/tmp";
    auto [new_id, new_fx] = request<to_agent::SessionNew>(client.engine, np);
    send(to_agent_wire, new_fx.send);
    deliver(agent, to_agent_wire, back);
    send(to_client_wire, back); back.clear();
    done = deliver(client, to_client_wire, back);
    auto sess = result<to_agent::SessionNew>(done.completed.at(0));
    CHECK(sess.has_value());
    CHECK(sess->sessionId.get() == "s1");

    // session/prompt: the agent defers, streams an update, asks permission,
    // then answers the prompt.
    PromptParams pp;
    pp.sessionId = sess->sessionId;
    auto [prompt_id, prompt_fx] = request<to_agent::SessionPrompt>(client.engine, pp);
    send(to_agent_wire, prompt_fx.send);
    auto agent_fx = deliver(agent, to_agent_wire, back);
    CHECK(back.empty());                         // deferred: no reply yet
    REQUIRE(agent_fx.calls.size() == 1);
    agent.prompt_call = agent_fx.calls[0].id;

    SessionUpdateMsg up;
    up.sessionId = sess->sessionId;
    up.update = SU_AgentMessageChunk{ContentBlock{TextContent{"hello", Nothing, Json::object()}},
                                     Nothing};
    to_client_wire.push_back(notify<to_client::SessionUpdate>(up));

    RequestPermissionParams rp;
    rp.sessionId = sess->sessionId;
    rp.options.push_back(PermissionOption{"allow_once", "Allow", PermissionOptionKind::AllowOnce,
                                          Json::object()});
    auto [perm_id, perm_fx] = request<to_client::RequestPermission>(agent.engine, rp);
    send(to_client_wire, perm_fx.send);

    deliver(client, to_client_wire, back);
    CHECK(client.updates.size() == 1);
    CHECK(client.permissions_asked == 1);
    send(to_agent_wire, back); back.clear();
    agent_fx = deliver(agent, to_agent_wire, back);
    REQUIRE(agent_fx.completed.size() == 1);
    CHECK(agent_fx.completed[0].id == perm_id);
    CHECK(result<to_client::RequestPermission>(agent_fx.completed[0]).has_value());

    PromptResult pr;
    pr.stopReason = StopReason::EndTurn;
    to_client_wire.push_back(reply<to_agent::SessionPrompt>(*agent.prompt_call, pr));
    done = deliver(client, to_client_wire, back);
    REQUIRE(done.completed.size() == 1);
    CHECK(done.completed[0].id == prompt_id);
    auto prompt = result<to_agent::SessionPrompt>(done.completed[0]);
    CHECK(prompt.has_value());
    CHECK(prompt->stopReason == StopReason::EndTurn);
    CHECK(client.engine.pending() == 0);
    CHECK(agent.engine.pending() == 0);
}

TEST_CASE("protocol: method types carry the wire names") {
    CHECK(to_agent::SessionPrompt::name == "session/prompt");
    CHECK(to_client::RequestPermission::name == "session/request_permission");
    CHECK(CancelRequest::name == "$/cancel_request");
    static_assert(jsonrpc::IsNote<to_agent::SessionCancel>);
    static_assert(jsonrpc::IsMethod<to_agent::SessionPrompt>);
}
