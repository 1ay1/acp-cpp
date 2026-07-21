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

struct ClientCapabilities {
    FsCapabilities fs{};
    bool terminal = false;
    Json meta = Json::object();           // custom extension capability declarations
};
template <> struct CodecOf<ClientCapabilities> {
    static Codec<ClientCapabilities> get() {
        return record<ClientCapabilities>(
            defaulted("fs",       &ClientCapabilities::fs,       FsCapabilities{}),
            defaulted("terminal", &ClientCapabilities::terminal, false),
            meta("_meta",    &ClientCapabilities::meta));
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
            optional("additionalDirectories", &SessionCapabilities::additionalDirectories));
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
    std::string type = "agent";    // default per spec
};
template <> struct CodecOf<AuthMethod> {
    static Codec<AuthMethod> get() {
        return record<AuthMethod>(
            required ("id",          &AuthMethod::id),
            required ("name",        &AuthMethod::name),
            optional ("description", &AuthMethod::description),
            defaulted("type",        &AuthMethod::type, std::string{"agent"}));
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
