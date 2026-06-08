// SPDX-License-Identifier: Apache-2.0
//
// acp/agent.hpp — high-level facade for clients (editors) talking to an agent.
//
//   "Agent" in the protocol means the LLM-side process. From the editor's
//   POV the agent is a remote service: we (the client) call methods on it,
//   and it sends notifications + a few callbacks back.
//
//   `AgentConnection` is the editor-side handle to that remote service:
//
//        ┌───────────────────────────────────────────────┐
//        │   AgentConnection (this header)               │
//        │                                               │
//        │   .initialize(...)                ──► Agent   │
//        │   .session_new(...)               ──► Agent   │
//        │   .session_prompt(...)            ──► Agent   │
//        │   .session_cancel(...)            ──► Agent   │ notification
//        │   ...                                         │
//        │                                               │
//        │   handlers = ClientHandlers{...}              │
//        │   on_session_update(...)        ◄── Agent     │
//        │   on_request_permission(...)    ◄── Agent     │
//        │   fs/* terminal/*               ◄── Agent     │
//        └───────────────────────────────────────────────┘
//
//   The handlers record is a product of std::functions; absent functions =
//   "method not implemented" → engine answers JSON-RPC MethodNotFound.
//
#pragma once

#include <acp/rpc.hpp>

namespace acp {

//==============================================================================
//  ClientHandlers — the editor-side surface the agent can call into.
//
//      Each field corresponds to one inbound method/notification. A null
//      std::function means "not implemented"; for requests the engine will
//      reply with MethodNotFound, for notifications it will be ignored.
//==============================================================================
struct ClientHandlers {
    // notifications — Agent → Client
    std::function<void(const SessionUpdateMsg&)>    on_session_update;

    // requests — Agent → Client
    std::function<RequestPermissionResult(const RequestPermissionParams&)>
        on_request_permission;

    std::function<ReadTextFileResult (const ReadTextFileParams&)>   on_fs_read_text_file;
    std::function<Unit               (const WriteTextFileParams&)>  on_fs_write_text_file;

    std::function<CreateTerminalResult     (const CreateTerminalParams&)>     on_terminal_create;
    std::function<TerminalOutputResult     (const TerminalOutputParams&)>     on_terminal_output;
    std::function<TerminalWaitForExitResult(const TerminalWaitForExitParams&)>on_terminal_wait_for_exit;
    std::function<Unit                     (const TerminalKillParams&)>       on_terminal_kill;
    std::function<Unit                     (const TerminalReleaseParams&)>    on_terminal_release;
};

//==============================================================================
//  AgentConnection — the editor's handle to a remote agent.
//==============================================================================
class AgentConnection {
public:
    explicit AgentConnection(Transport sink, ClientHandlers handlers = {})
        : engine_(std::move(sink)) {
        install_handlers(std::move(handlers));
    }

    RpcEngine& engine() noexcept { return engine_; }

    // -------------------------------------------------------- outbound calls

    std::future<InitializeResult> initialize(const InitializeParams& p) {
        return engine_.request<InitializeResult>(method::Initialize, p);
    }
    std::future<Unit> authenticate(const AuthenticateParams& p) {
        return engine_.request<Unit>(method::Authenticate, p);
    }
    std::future<Unit> logout() {
        return engine_.request<Unit>(method::Logout);
    }

    std::future<NewSessionResult> session_new(const NewSessionParams& p) {
        return engine_.request<NewSessionResult>(method::SessionNew, p);
    }
    std::future<Unit> session_load(const LoadSessionParams& p) {
        return engine_.request<Unit>(method::SessionLoad, p);
    }
    std::future<ResumeSessionResult> session_resume(const ResumeSessionParams& p) {
        return engine_.request<ResumeSessionResult>(method::SessionResume, p);
    }
    std::future<Unit> session_close(const CloseSessionParams& p) {
        return engine_.request<Unit>(method::SessionClose, p);
    }
    std::future<Unit> session_delete(const DeleteSessionParams& p) {
        return engine_.request<Unit>(method::SessionDelete, p);
    }
    std::future<ListSessionsResult> session_list(const ListSessionsParams& p) {
        return engine_.request<ListSessionsResult>(method::SessionList, p);
    }

    std::future<PromptResult> session_prompt(const PromptParams& p) {
        return engine_.request<PromptResult>(method::SessionPrompt, p);
    }
    void session_cancel(const CancelParams& p) {
        engine_.notify(method::SessionCancel, p);
    }

    std::future<Unit> session_set_mode(const SetModeParams& p) {
        return engine_.request<Unit>(method::SessionSetMode, p);
    }
    std::future<SetConfigOptionResult> session_set_config_option(const SetConfigOptionParams& p) {
        return engine_.request<SetConfigOptionResult>(method::SessionSetConfig, p);
    }

    // ----------------------------------------------------------- ext methods
    std::future<Json> ext_request(std::string_view m, const Json& p = Json::object()) {
        return engine_.ext_request(m, p);
    }
    void ext_notify(std::string_view m, const Json& p = Json::object()) {
        engine_.ext_notify(m, p);
    }

    // Capability negotiation result — cached after initialize() resolves so we
    // can refuse to send capability-gated calls. Users can also poll directly.
    void remember_negotiated(const InitializeResult& r) noexcept {
        negotiated_ = r;
    }
    const Maybe<InitializeResult>& negotiated() const noexcept { return negotiated_; }

private:
    void install_handlers(ClientHandlers h) {
        auto bind_req = [&](std::string_view m, auto& slot) {
            using P = typename std::decay_t<decltype(slot)>::argument_type;
            // std::function::argument_type isn't standard; use the explicit form below.
            (void)slot; (void)m;
        };
        // The actual installations — pattern: only register if the handler is set.
        if (h.on_session_update) {
            engine_.on_note<SessionUpdateMsg>(std::string(method::SessionUpdate),
                std::move(h.on_session_update));
        }
        if (h.on_request_permission) {
            engine_.on<RequestPermissionParams, RequestPermissionResult>(
                std::string(method::RequestPermission), std::move(h.on_request_permission));
        }
        if (h.on_fs_read_text_file) {
            engine_.on<ReadTextFileParams, ReadTextFileResult>(
                std::string(method::FsReadTextFile), std::move(h.on_fs_read_text_file));
        }
        if (h.on_fs_write_text_file) {
            engine_.on<WriteTextFileParams, Unit>(
                std::string(method::FsWriteTextFile), std::move(h.on_fs_write_text_file));
        }
        if (h.on_terminal_create) {
            engine_.on<CreateTerminalParams, CreateTerminalResult>(
                std::string(method::TerminalCreate), std::move(h.on_terminal_create));
        }
        if (h.on_terminal_output) {
            engine_.on<TerminalOutputParams, TerminalOutputResult>(
                std::string(method::TerminalOutput), std::move(h.on_terminal_output));
        }
        if (h.on_terminal_wait_for_exit) {
            engine_.on<TerminalWaitForExitParams, TerminalWaitForExitResult>(
                std::string(method::TerminalWaitForExit), std::move(h.on_terminal_wait_for_exit));
        }
        if (h.on_terminal_kill) {
            engine_.on<TerminalKillParams, Unit>(
                std::string(method::TerminalKill), std::move(h.on_terminal_kill));
        }
        if (h.on_terminal_release) {
            engine_.on<TerminalReleaseParams, Unit>(
                std::string(method::TerminalRelease), std::move(h.on_terminal_release));
        }
    }

    RpcEngine engine_;
    Maybe<InitializeResult> negotiated_;
};

} // namespace acp
