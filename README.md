# acp-cpp

**The [Agent Client Protocol](https://agentclientprotocol.com) as C++ types.**

A header-only C++23 library: ACP's schema and method table, on top of
[jsonrpc-cpp](https://github.com/1ay1/jsonrpc-cpp). It has no runtime. No
threads, locks, futures, clocks or transports. The host owns the connection:
it feeds bytes into a `jsonrpc::Engine`, performs the effects that come back,
and dispatches the peer's calls with a `jsonrpc::Router`.

```
┌─────────────┐                ACP                ┌─────────────┐
│   Editor    │ ◄──────  JSON-RPC over stdio ────►│    Agent    │
│ Engine +    │                                   │ Engine +    │
│ Router      │                                   │ Router      │
└─────────────┘                                   └─────────────┘
```

---

## Design

ACP is a closed algebra of types: sums, products, optionals, lists. The
library keeps that structure instead of handing out `nlohmann::json` blobs.

| Layer | Headers | Role |
|------|---------|------|
| **Algebra**  | `core.hpp`, `codec.hpp` | `Unit`, `Maybe<A>`, `List<A>`, `Sum<…>`, `Newtype<P,A>` and the codec combinators, from jsonrpc-cpp, re-exported into `acp::` |
| **Schema**   | `ids.hpp`, `content.hpp`, `tools.hpp`, `session_types.hpp`, `updates.hpp`, `caps.hpp`, `methods.hpp` | Every ACP type as an expression in the algebra. No hand-written `to_json` |
| **Methods**  | `protocol.hpp` | Every ACP method as a type: `to_agent::SessionPrompt`, `to_client::RequestPermission`, … Name, params and result are declared once |

The single include is `<acp/acp.hpp>`.

Because a method is a type, a request, its handler and its reply are checked
against the same declaration:

```cpp
auto [id, fx] = acp::request<acp::to_agent::SessionNew>(engine, params);
router.on<acp::to_client::RequestPermission>([](Ctx&, const acp::RequestPermissionParams& p) {
    return acp::RequestPermissionResult{ /* … */ };
});
auto res = acp::result<acp::to_agent::SessionNew>(completed);   // expected<NewSessionResult, RpcError>
```

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
string-tagged enums — all of it lives in the codec combinators and falls
out cleanly into every protocol type.

---

## Usage

One engine per connection. Everything it does is a value you act on:

```cpp
#include <acp/acp.hpp>
using namespace acp;

struct Agent { /* your state */ };

Router<Agent> router;
router.on<to_agent::Initialize>([](Agent&, const InitializeParams& p) {
    InitializeResult r;
    r.protocolVersion = negotiate_version(kProtocolVersion, p.protocolVersion);
    return r;
});
router.defer<to_agent::SessionPrompt>();   // answered later with reply<SessionPrompt>

Engine engine;
// For every line read from the transport:
Effects fx = step(engine, Received{line});
for (auto& frame : fx.send) write(frame);
for (auto& call : fx.calls)
    if (auto frame = router.handle(agent, call)) write(*frame);
for (auto& note : fx.notifications) router.handle(agent, note);
for (auto& done : fx.completed) /* your own requests, answered */;

// Deadlines: step(engine, Tick{now}) when engine.next_deadline() passes.
// End of stream: step(engine, Closed{}) fails everything still pending.
```

Threads, timers and I/O are the host's choice. `tests/protocol_loopback.cpp`
runs a whole client/agent exchange on one thread by passing frames between
two engines.

---

## Building

Requirements: a C++23 compiler and CMake 3.20+. The build pulls
`nlohmann/json` and `jsonrpc-cpp` via `FetchContent` when they aren't already
targets.

```sh
cmake -S . -B build -G Ninja
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The tests build without `-pthread`: if the library ever needs a thread, they
fail to link.

Use it from another CMake project:

```cmake
include(FetchContent)
FetchContent_Declare(acp-cpp GIT_REPOSITORY https://github.com/1ay1/acp-cpp.git GIT_TAG main)
FetchContent_MakeAvailable(acp-cpp)

target_link_libraries(my_thing PRIVATE acp::acp)
```

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

---

## Project layout

```
include/acp/
  acp.hpp              ← single umbrella include
  version.hpp          ← library + protocol version constants (generated)
  core.hpp             ← the algebra, re-exported from jsonrpc-cpp
  codec.hpp            ← codec combinators, re-exported from jsonrpc-cpp
  ids.hpp              ← SessionId, ToolCallId, SessionModeId, MessageId
  content.hpp          ← ContentBlock and its constituents
  tools.hpp            ← ToolCall, ToolCallUpdate, PlanEntry, permissions
  session_types.hpp    ← McpServer, modes, config options, slash cmds, usage
  updates.hpp          ← SessionUpdate (11-arm sum)
  caps.hpp             ← capabilities, version negotiation, AuthMethod
  methods.hpp          ← Params/Result records for every wire method
  protocol.hpp         ← every method as a type, plus the engine vocabulary

tests/
  kernel_smoke.cpp       ← codec algebra round-trips
  content_smoke.cpp      ← ContentBlock round-trips
  spec_round_trip.cpp    ← every method's Params/Result round-trips
  schema_conformance.cpp ← wire spellings and _meta omission
  protocol_loopback.cpp  ← a client and an agent, two engines, no threads
```

---

## Status

The schema covers ACP v1 in full. The API is still settling.

## License

Apache 2.0.
