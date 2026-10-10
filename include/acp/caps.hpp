// SPDX-License-Identifier: Apache-2.0
//
// acp/caps.hpp — capability lattice and AuthMethod.
//
//   The protocol's capability system is a small lattice of optional features
//   negotiated at `initialize`. We model "supported" as Just<Unit> and
//   "unsupported" as Nothing — matching the spec's "presence ⇒ supported"
//   convention literally.
//
//   For richer capabilities (PromptCapabilities, McpCapabilities) we use
//   booleans because the spec attaches no payload beyond yes/no.
//
//   Implementations advertise extension capabilities via _meta (acp::Json).
//
#pragma once

#include <acp/session_types.hpp>

#include <map>
#include <stdexcept>

namespace acp {

//==============================================================================
//  CapabilityError — thrown locally when a caller invokes a method whose
//  capability the peer did not advertise at initialize. Caught early, before
//  a round-trip, instead of waiting for the peer's MethodNotFound.
//==============================================================================
struct CapabilityError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

//==============================================================================
//  Protocol-version negotiation. Per the spec, peers agree on the minimum of
//  the two supported versions. `negotiate_version` returns the version both
//  sides can speak, or throws if there is no overlap (theirs is older than the
//  oldest we support, i.e. < 1).
//
//  Forward-compat / v2 seam: `kProtocolVersion` is the STABLE version we
//  advertise by default (v1). ACP v2 is Draft (announced 2026-07-20) and the
//  spec is explicit that implementers must gate it behind version negotiation
//  AND a feature flag, and must NOT ship it by default. So the highest version
//  we are willing to negotiate is `kMaxProtocolVersion`, which equals
//  `kProtocolVersion` unless the build opts in via -DACP_ENABLE_V2_DRAFT=ON
//  (which defines ACP_ENABLE_V2_DRAFT). Because negotiation is min(ours,
//  theirs), advertising a higher ceiling is safe: a v1-only peer still lands on
//  v1. Raising the ceiling does not by itself change any wire types — it only
//  lets a v2-capable peer agree on v2 so v2 code paths can key off the result.
//==============================================================================
#if defined(ACP_ENABLE_V2_DRAFT)
inline constexpr int kMaxProtocolVersion = 2;   // Draft v2 opted in at build time
#else
inline constexpr int kMaxProtocolVersion = kProtocolVersion;   // stable only (v1)
#endif

inline int negotiate_version(int ours, int theirs) {
    int agreed = ours < theirs ? ours : theirs;
    if (agreed < 1)
        throw CapabilityError("no compatible ACP protocol version (peer offered " +
                              std::to_string(theirs) + ", we support " +
                              std::to_string(ours) + ")");
    return agreed;
}

//==============================================================================
//  Client capabilities  (FsCapabilities × terminal)
//==============================================================================
struct FsCapabilities {
    bool readTextFile  = false;
    bool writeTextFile = false;
};
template <> struct CodecOf<FsCapabilities> {
    static Codec<FsCapabilities> get() {
        return record<FsCapabilities>(
            defaulted("readTextFile",  &FsCapabilities::readTextFile,  false),
            defaulted("writeTextFile", &FsCapabilities::writeTextFile, false));
    }
};

// clientCapabilities.session (v1.11): what the client can render. Each is
// "present means supported"; the agent MUST NOT send notice or compaction
// updates, or boolean config options, unless the matching field is set.
struct ClientSessionCapabilities {
    bool compaction     = false;
    bool notices        = false;
    bool booleanConfig  = false;   // configOptions.boolean
    Json meta = Json::object();
};
template <> struct CodecOf<ClientSessionCapabilities> {
    static Codec<ClientSessionCapabilities> get() {
        auto present = [](const Json& j, const char* k) {
            return j.is_object() && j.contains(k) && j[k].is_object();
        };
        return {
            [](const ClientSessionCapabilities& c) -> Json {
                Json j = Json::object();
                if (c.compaction) j["compaction"] = Json::object();
                if (c.notices)    j["notices"]    = Json::object();
                if (c.booleanConfig) j["configOptions"] = Json{{"boolean", Json::object()}};
                if (!c.meta.empty()) j["_meta"] = c.meta;
                return j;
            },
            [present](const Json& j) -> ClientSessionCapabilities {
                ClientSessionCapabilities c;
                c.compaction = present(j, "compaction");
                c.notices    = present(j, "notices");
                c.booleanConfig = present(j, "configOptions") && present(j["configOptions"], "boolean");
                if (j.is_object() && j.contains("_meta") && j["_meta"].is_object()) c.meta = j["_meta"];
                return c;
            }};
    }
};

struct ClientCapabilities {
    FsCapabilities fs{};
    bool terminal = false;
    ClientSessionCapabilities session{};
    bool authTerminal = false;            // auth.terminal: runs terminal login methods
    Json meta = Json::object();           // custom extension capability declarations
};
template <> struct CodecOf<ClientCapabilities> {
    static Codec<ClientCapabilities> get() {
        auto base = record<ClientCapabilities>(
            defaulted("fs",       &ClientCapabilities::fs,       FsCapabilities{}),
            defaulted("terminal", &ClientCapabilities::terminal, false),
            defaulted("session",  &ClientCapabilities::session,  ClientSessionCapabilities{}),
            meta("_meta",    &ClientCapabilities::meta));
        return {
            [base](const ClientCapabilities& c) -> Json {
                Json j = base.encode(c);
                if (c.authTerminal) j["auth"] = Json{{"terminal", true}};
                return j;
            },
            [base](const Json& j) -> ClientCapabilities {
                ClientCapabilities c = base.decode(j);
                if (j.is_object() && j.contains("auth") && j["auth"].is_object()) {
                    const Json& a = j["auth"];
                    c.authTerminal = a.contains("terminal") && a["terminal"].is_boolean()
                                  && a["terminal"].get<bool>();
                }
                return c;
            }};
    }
};

//==============================================================================
//  Agent capabilities.
//==============================================================================
struct PromptCapabilities {
    bool image           = false;
    bool audio           = false;
    bool embeddedContext = false;
};
template <> struct CodecOf<PromptCapabilities> {
    static Codec<PromptCapabilities> get() {
        return record<PromptCapabilities>(
            defaulted("image",           &PromptCapabilities::image,           false),
            defaulted("audio",           &PromptCapabilities::audio,           false),
            defaulted("embeddedContext", &PromptCapabilities::embeddedContext, false));
    }
};

struct McpCapabilities {
    bool http = false;
    bool sse  = false;
};
template <> struct CodecOf<McpCapabilities> {
    static Codec<McpCapabilities> get() {
        return record<McpCapabilities>(
            defaulted("http", &McpCapabilities::http, false),
            defaulted("sse",  &McpCapabilities::sse,  false));
    }
};

// AuthCapabilities  ≅  { logout? : Unit }
struct AuthCapabilities {
    Maybe<Unit> logout;
};
template <> struct CodecOf<AuthCapabilities> {
    static Codec<AuthCapabilities> get() {
        return record<AuthCapabilities>(
            optional("logout", &AuthCapabilities::logout));
    }
};

// SessionCapabilities — each feature is Maybe<Unit> (present-empty-object ⇒ on).
struct SessionCapabilities {
    Maybe<Unit> deleteCap;             // C++ reserved word avoidance
    Maybe<Unit> resume;
    Maybe<Unit> close;
    Maybe<Unit> list;
    Maybe<Unit> additionalDirectories;
    Maybe<Unit> fork;                  // unstable session/fork
};

template <> struct CodecOf<SessionCapabilities> {
    static Codec<SessionCapabilities> get() {
        // We can't use `optional("delete", &SessionCapabilities::deleteCap)`
        // through the record combinator because the type is the same as the
        // other optionals; pointer-to-member already handles the C++ naming.
        // The combinator emits the wire key "delete" exactly as supplied.
        return record<SessionCapabilities>(
            optional("delete",                &SessionCapabilities::deleteCap),
            optional("resume",                &SessionCapabilities::resume),
            optional("close",                 &SessionCapabilities::close),
            optional("list",                  &SessionCapabilities::list),
            optional("additionalDirectories", &SessionCapabilities::additionalDirectories),
            optional("fork",                  &SessionCapabilities::fork));
    }
};

struct AgentCapabilities {
    bool loadSession = false;
    PromptCapabilities  promptCapabilities{};
    McpCapabilities     mcpCapabilities{};
    AuthCapabilities    auth{};
    SessionCapabilities sessionCapabilities{};
    Json meta = Json::object();
};
template <> struct CodecOf<AgentCapabilities> {
    static Codec<AgentCapabilities> get() {
        return record<AgentCapabilities>(
            defaulted("loadSession",         &AgentCapabilities::loadSession,         false),
            defaulted("promptCapabilities",  &AgentCapabilities::promptCapabilities,  PromptCapabilities{}),
            defaulted("mcpCapabilities",     &AgentCapabilities::mcpCapabilities,     McpCapabilities{}),
            defaulted("auth",                &AgentCapabilities::auth,                AuthCapabilities{}),
            defaulted("sessionCapabilities", &AgentCapabilities::sessionCapabilities, SessionCapabilities{}),
            meta("_meta",               &AgentCapabilities::meta));
    }
};

//==============================================================================
//  AuthMethod
//==============================================================================
struct AuthMethod {
    std::string id;
    std::string name;
    Maybe<std::string> description;
    std::string type = "agent";    // "agent" (default) | "terminal"
    // terminal only: the editor runs the agent binary with these args and
    // env in an interactive terminal, then retries.
    List<std::string> args;
    std::map<std::string, std::string> env;
    Json meta = Json::object();
};
template <> struct CodecOf<AuthMethod> {
    static Codec<AuthMethod> get() {
        return {
            [](const AuthMethod& m) -> Json {
                Json j{{"id", m.id}, {"name", m.name}};
                if (m.description) j["description"] = *m.description;
                if (m.type != "agent") j["type"] = m.type;
                if (m.type == "terminal") {
                    if (!m.args.empty()) j["args"] = m.args;
                    if (!m.env.empty())  j["env"]  = m.env;
                }
                if (!m.meta.empty()) j["_meta"] = m.meta;
                return j;
            },
            [](const Json& j) -> AuthMethod {
                if (!j.is_object()) throw CodecError("AuthMethod: expected object");
                AuthMethod m;
                m.id   = j.at("id").get<std::string>();
                m.name = j.at("name").get<std::string>();
                if (j.contains("description") && j["description"].is_string())
                    m.description = Just(j["description"].get<std::string>());
                m.type = j.value("type", std::string{"agent"});
                if (j.contains("args") && j["args"].is_array())
                    m.args = j["args"].get<List<std::string>>();
                if (j.contains("env") && j["env"].is_object())
                    m.env = j["env"].get<std::map<std::string, std::string>>();
                if (j.contains("_meta") && j["_meta"].is_object()) m.meta = j["_meta"];
                return m;
            }};
    }
};

//==============================================================================
//  Implementation info  (clientInfo / agentInfo).
//==============================================================================
struct ImplementationInfo {
    std::string name;
    Maybe<std::string> title;
    Maybe<std::string> version;
};
template <> struct CodecOf<ImplementationInfo> {
    static Codec<ImplementationInfo> get() {
        return record<ImplementationInfo>(
            required ("name",    &ImplementationInfo::name),
            optional ("title",   &ImplementationInfo::title),
            optional ("version", &ImplementationInfo::version));
    }
};

} // namespace acp
