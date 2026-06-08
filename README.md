# acp-cpp

**The first C++ implementation of the [Agent Client Protocol](https://agentclientprotocol.com).**

A modern (C++20), header-only, type-theoretically grounded library that lets any
editor talk to any ACP agent, and any agent talk to any ACP editor — over stdio
today, over arbitrary transports tomorrow.

```
┌─────────────┐                ACP                ┌─────────────┐
│   Editor    │ ◄──────  JSON-RPC over stdio ────►│    Agent    │
│             │                                   │             │
│ AgentConn.  │                                   │ ClientConn. │
└─────────────┘                                   └─────────────┘
```

---

## Design philosophy

ACP is a *protocol*. A protocol is, mathematically, a closed algebra of
inductive types: sums, products, optionals, lists, recursive types. Most
JSON-RPC libraries throw this structure away — they expose strings and
`nlohmann::json` blobs and ask the user to remember which keys mean which
things. We don't.

The library is **four layers**, each smaller than the last:

| Layer | Headers | Role |
|------|---------|------|
| **1. Algebra**  | `core.hpp`, `codec.hpp` | `Unit`, `Maybe<A>`, `List<A>`, `Sum<…>`, `Newtype<P,A>`, and the codec combinators `record`, `sum_tagged`, `enum_codec`, `list_codec`, `maybe_codec`, `meta` |
| **2. Schema**   | `ids.hpp`, `content.hpp`, `tools.hpp`, `session_types.hpp`, `updates.hpp`, `caps.hpp`, `methods.hpp` | Every ACP type as a closed algebraic expression in layer 1 — never any hand-written `to_json` |
| **3. Engine**   | `rpc.hpp` | JSON-RPC 2.0 dispatcher: typed `request<Result, Params>` → `std::future<Result>`, typed `on<Params, Result>(handler)`, full bidirectional, per-request timeouts, wire-tracing + error hooks |
| **4. Transport / Facade** | `stdio.hpp`, `agent.hpp`, `client.hpp` | Line-framed stdio + two role facades: `AgentConnection` (editor side) and `ClientConnection` (agent side) |
| **5. Coroutines** *(opt-in)* | `coro.hpp` | `Task<T>` + `co_await` over any returned `std::future<T>` |

Single include is `<acp/acp.hpp>` (layers 1–4). Add `<acp/coro.hpp>` for the
coroutine surface — it is intentionally NOT pulled in by the umbrella header, so
you pay nothing for coroutines unless you ask for them.

---

## Engineering guarantees

Beyond spec coverage, the engine is built for production use:

- **Codec caching** — `codec<T>()` builds each type's codec tree exactly once
  (function-local `static`) and returns a reference; encode/decode no longer
  allocate a fresh `std::function` tree per message.
- **Per-request timeouts** — `request(..., 5s)` or `set_default_timeout(...)`;
  an elapsed deadline fails the future with `RpcError(errc::Timeout)`. A lazy
  background monitor thread starts only when the first timed request is made.
- **Promise-backed futures** — `request_raw(...)` returns a future fulfilled
  directly by the reader thread, so `wait_for` / `wait_until` work correctly
  (a `std::async(deferred)` future would report `deferred` and never block).
- **Robust id handling** — in-flight requests are keyed on the canonical text
  form of the JSON-RPC id (`id.dump()`), so string ids, integer ids, and any
  other valid id round-trip exactly. No coercion, no leaked waiters.
- **Connection-loss + error surface** — `set_error_callback(...)` and, on the
  stdio transport, EOF fires `on_transport_closed`, failing every in-flight
  request with `errc::ConnectionLost`.
- **Wire tracing** — `set_wire_trace([](WireDir, std::string_view){ ... })` sees
  every frame in both directions for debugging.
- **Capability + version gating** — `initialize` negotiates the protocol
  version and caches the peer's capabilities; capability-gated calls
  (`session_resume`, `logout`, …) throw `CapabilityError` locally instead of
  doing a round-trip to learn `MethodNotFound`.
- **Race-free** — the full test suite passes clean under ThreadSanitizer and
  ASan/UBSan (`-DACP_SANITIZE=thread|address`).

---

## A type-theoretic look

Every protocol type is denoted by a single algebraic expression. The codec
follows the shape of the type; serialization is a fold, never bespoke code.

For example, the spec's `ContentBlock` reads:

> A ContentBlock is one of: Text { text }, Image { data; mimeType; uri? },
> Audio { data; mimeType }, ResourceLink { uri; name; … },
> Resource { resource: TextResource | BlobResource }.

In `content.hpp` this becomes literally:

```cpp
using EmbeddedResource = Sum<TextResource, BlobResource>;

using ContentBlock = Sum<
    TextContent,
    ImageContent,
    AudioContent,
    ResourceLinkContent,
    ResourceContent>;

template <> struct CodecOf<ContentBlock> {
    static Codec<ContentBlock> get() {
        return sum_tagged<ContentBlock>("type",
            arm<ContentBlock, TextContent>        ("text"),
            arm<ContentBlock, ImageContent>       ("image"),
            arm<ContentBlock, AudioContent>       ("audio"),
            arm<ContentBlock, ResourceLinkContent>("resource_link"),
            arm<ContentBlock, ResourceContent>    ("resource"));
    }
};
```

Each record is the same:

```cpp
template <> struct CodecOf<NewSessionParams> {
    static Codec<NewSessionParams> get() {
        return record<NewSessionParams>(
            required ("cwd",                   &NewSessionParams::cwd),
            defaulted("mcpServers",            &NewSessionParams::mcpServers, List<McpServer>{}),
            optional ("additionalDirectories", &NewSessionParams::additionalDirectories));
    }
};
```

The field combinators (`required`, `optional`, `defaulted`, `meta`) cover every
shape the spec actually uses, and the spec's `_meta` extension field has its
own combinator that knows to omit empty objects from the wire — exactly as the
spec dictates.

Tagged sums, products with optional fields, opaque ids (via `Newtype`),
string-tagged enums — all of it lives in 200 lines of `codec.hpp` and falls
out cleanly into every protocol type.

---

## Usage: writing an agent

```cpp
#include <acp/acp.hpp>
using namespace acp;

int main() {
    AgentHandlers h;
    std::shared_ptr<ClientConnection> conn;

    h.on_initialize = [](const InitializeParams&) {
        InitializeResult r;
        r.agentCapabilities.promptCapabilities.embeddedContext = true;
        r.agentInfo = Just<ImplementationInfo>({"my-agent", Just("My Agent"), Just("1.0")});
        return r;
    };
    h.on_session_new = [](const NewSessionParams&) {
        return NewSessionResult{SessionId{std::string("sess_1")}, Nothing, Nothing, Json::object()};
    };
    h.on_session_prompt = [&](const PromptParams& p) {
        SessionUpdateMsg upd;
        upd.sessionId = p.sessionId;
        upd.update = SU_AgentMessageChunk{
            TextContent{"Hello!", Nothing, Json::object()}, Nothing};
        conn->session_update(upd);
        return PromptResult{StopReason::EndTurn};
    };
    h.on_session_cancel = [](const CancelParams&) {};

    StdioTransport tx(std::cin, std::cout);
    conn = std::make_shared<ClientConnection>(tx.sink(), std::move(h));
    tx.start(conn->engine());
    tx.join();
}
```

That's a fully working ACP agent. `examples/echo_agent.cpp` is the same
thing with a plan and an actual echo.

---

## Usage: writing a client (editor)

```cpp
#include <acp/acp.hpp>
using namespace acp;

int main() {
    // Suppose you've spawned an agent subprocess and have streams over its pipes:
    extern std::istream& agent_out;
    extern std::ostream& agent_in;

    ClientHandlers h;
    h.on_session_update = [](const SessionUpdateMsg& m) {
        match(m.update,
            [](const SU_AgentMessageChunk& c) {
                match(c.content,
                    [](const TextContent& t) { std::cout << t.text; },
                    [](const auto&) {});
            },
            [](const auto&) {});
    };
    h.on_fs_read_text_file = [](const ReadTextFileParams& p) {
        // serve unsaved editor buffers here
        return ReadTextFileResult{"…file contents…"};
    };

    StdioTransport tx(agent_out, agent_in);
    AgentConnection agent(tx.sink(), std::move(h));
    tx.start(agent.engine());

    InitializeParams ip;
    ip.clientCapabilities.fs.readTextFile  = true;
    ip.clientCapabilities.fs.writeTextFile = true;
    ip.clientCapabilities.terminal         = true;
    auto info = agent.initialize(ip).get();

    auto sess = agent.session_new(NewSessionParams{".", {}, Nothing}).get();
    PromptParams pp;
    pp.sessionId = sess.sessionId;
    pp.prompt.push_back(TextContent{"Hello, agent!", Nothing, Json::object()});
    auto result = agent.session_prompt(pp).get();
    // result.stopReason ∈ {EndTurn, MaxTokens, MaxTurnRequests, Refusal, Cancelled}
}
```

See `examples/minimal_client.cpp` for the full POSIX `fork+exec` version that
actually spawns a child agent process.

---

## Usage: coroutines

Include `<acp/coro.hpp>` and the same handshake reads as straight-line code —
each `co_await` suspends until the reply lands, without blocking a thread:

```cpp
#include <acp/acp.hpp>
#include <acp/coro.hpp>
using namespace acp;

Task<void> drive(AgentConnection& agent) {
    auto ir   = co_await agent.initialize(ip);
    auto sess = co_await agent.session_new({".", {}, Nothing});
    PromptParams pp; pp.sessionId = sess.sessionId;
    pp.prompt.push_back(TextContent{"hello", Nothing, Json::object()});
    auto pr   = co_await agent.session_prompt(pp);
    // pr.stopReason ...
    co_return;
}

int main() {
    /* wire up transport + AgentConnection as usual */
    drive(agent).get();   // synchronous entry point for non-coroutine callers
}
```

`co_await` works on any `std::future<T>` the API returns; `Task<T>` composes,
so tasks can `co_await` other tasks. See `examples/coro_client.cpp` for the full
runnable version. The blocking bridge (`Task::get()`) is synchronized, not a
busy-spin — it passes ThreadSanitizer clean.

---

## Building

Requirements: a C++20 compiler (GCC 12+, Clang 15+, MSVC 19.30+) and CMake 3.20+.
The build pulls `nlohmann/json` via `FetchContent`. No other dependencies.

```sh
cmake -S . -B build -G Ninja
cmake --build build -j
ctest --test-dir build --output-on-failure
```

To run the suite under sanitizers (CI does both on every push):

```sh
cmake -S . -B build-tsan -G Ninja -DACP_SANITIZE=thread     # ThreadSanitizer
cmake -S . -B build-asan -G Ninja -DACP_SANITIZE=address    # ASan + UBSan
```

Use it from another CMake project:

```cmake
include(FetchContent)
FetchContent_Declare(acp-cpp GIT_REPOSITORY <…> GIT_TAG main)
FetchContent_MakeAvailable(acp-cpp)

target_link_libraries(my_thing PRIVATE acp::acp)
```

The library is **header-only**; no source files to compile separately.

---

## Coverage of the ACP spec

| Category | Methods / types | Status |
|----------|-----------------|--------|
| Init / auth   | `initialize`, `authenticate`, `logout` | ✅ |
| Sessions      | `session/new`, `session/load`, `session/resume`, `session/close`, `session/delete`, `session/list` | ✅ |
| Prompt turn   | `session/prompt`, `session/cancel`, `StopReason` | ✅ |
| Streaming     | All 11 variants of `session/update` (`agent_message_chunk`, `agent_thought_chunk`, `user_message_chunk`, `plan`, `tool_call`, `tool_call_update`, `available_commands_update`, `current_mode_update`, `config_option_update`, `session_info_update`, `usage_update`) | ✅ |
| Permissions   | `session/request_permission`, all four `PermissionOptionKind`s, both outcomes | ✅ |
| Content       | All five `ContentBlock` variants, `EmbeddedResource = TextResource + BlobResource` | ✅ |
| Tool calls    | `ToolCall`, `ToolCallUpdate`, all ten `ToolKind`s, all four statuses, `Content + Diff + Terminal` content, locations | ✅ |
| Plans         | `PlanEntry`, three priorities, three statuses | ✅ |
| Modes         | `session/set_mode`, `SessionModeState`, mode notifications | ✅ |
| Config opts   | `session/set_config_option`, `ConfigOption`, three reserved categories + custom | ✅ |
| Slash cmds    | `available_commands_update` notification | ✅ |
| Filesystem    | `fs/read_text_file`, `fs/write_text_file` | ✅ |
| Terminals     | `terminal/create`, `/output`, `/wait_for_exit`, `/kill`, `/release`, `TerminalExitStatus` | ✅ |
| MCP servers   | All three transports: stdio (mandatory), HTTP, SSE | ✅ |
| Session info  | `session/list` pagination, `session_info_update`, `SessionInfo` | ✅ |
| Usage         | `Cost`, `usage_update` token counts | ✅ |
| Extensibility | `_meta` on every type, `_`-prefixed custom methods (via raw `notify_raw`/`request_raw`) | ✅ |

This library targets **ACP protocol version 1** (`kProtocolVersion == 1`),
verified field-by-field against `schema/v1/schema.json`. Every data field of
all 135 schema definitions is present and correctly named, every enum carries
the exact wire spellings, and the spec's `_meta` extension object round-trips
on each type that defines it. Empty `_meta` is omitted from the wire per spec;
the terminal `Unit` responses (logout/authenticate/write/kill/release, etc.)
tolerate and ignore any `_meta` a peer attaches.

Optional capabilities are gated **locally** by the facades: `initialize`
negotiates the protocol version and caches the peer's advertised capabilities,
so a capability-gated call (`session_resume`, `session_load`, `logout`, …)
throws `CapabilityError` before it ever hits the wire — no round-trip to learn
the peer would have answered `MethodNotFound`. You can still inspect
`AgentConnection::negotiated()` / `ClientConnection::negotiated()` directly.

---

## Project layout

```
include/acp/
  acp.hpp              ← single umbrella include
  version.hpp          ← library + protocol version constants
  json.hpp             ← nlohmann::json re-export
  core.hpp             ← Unit, Maybe, List, Sum, Newtype, Tag, match
  codec.hpp            ← Codec<T>, record, sum_tagged, enum_codec, meta, …
  ids.hpp              ← SessionId, ToolCallId, SessionModeId, MessageId
  content.hpp          ← ContentBlock and its constituents
  tools.hpp            ← ToolCall, ToolCallUpdate, PlanEntry, permissions
  session_types.hpp    ← McpServer, modes, config options, slash cmds, usage
  updates.hpp          ← SessionUpdate (11-arm coproduct)
  caps.hpp             ← capability negotiation surface + AuthMethod
  methods.hpp          ← Params/Result records for every wire method
  rpc.hpp              ← bidirectional JSON-RPC 2.0 engine
  stdio.hpp            ← line-framed stdio transport
  agent.hpp            ← AgentConnection (editor uses this)
  client.hpp           ← ClientConnection (agent uses this)
  coro.hpp             ← Task<T> + co_await over std::future (opt-in)

examples/
  echo_agent.cpp       ← minimal but real agent
  minimal_client.cpp   ← POSIX client that spawns an agent and drives a turn
  coro_client.cpp      ← the same client written with C++20 coroutines

tests/
  kernel_smoke.cpp     ← codec algebra round-trips
  content_smoke.cpp    ← ContentBlock round-trips
  spec_round_trip.cpp  ← every method's Params/Result round-trips
  loopback.cpp         ← two engines, two threads, one real protocol exchange
  engine_features.cpp  ← timeouts, id keying, tracing, gating, coroutines
  schema_conformance.cpp ← wire-spelling + _meta-omission invariants
```

---

## Status

Early. The API is in flux until a few real downstream users have shaken it out.
The protocol coverage is complete for ACP v1; the engine + stdio transport pass
a real loopback integration test plus a dedicated engine-features suite, and the
whole suite is clean under ThreadSanitizer and ASan/UBSan in CI across
GCC/Clang/MSVC. The coroutine layer is opt-in.

PRs welcome — start with an issue describing the use case.

## License

Apache 2.0.
