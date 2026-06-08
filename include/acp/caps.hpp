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
//==============================================================================
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
