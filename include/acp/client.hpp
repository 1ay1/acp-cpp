// SPDX-License-Identifier: Apache-2.0
//
// acp/client.hpp — high-level facade for agents talking to a host editor.
//
//   "Client" in the protocol means the editor process. From the agent's
//   POV the client is a remote service: we (the agent) handle incoming
//   protocol methods, and we make outbound notifications + a few callbacks.
//
//   `ClientConnection` is the agent-side handle to that remote service.
//
//        ┌───────────────────────────────────────────────┐
//        │   ClientConnection                            │
//        │                                               │
//        │   .session_update(...)        ──► Client      │ notification
//        │   .request_permission(...)    ──► Client      │
//        │   .fs_read_text_file(...)     ──► Client      │
//        │   .terminal_*(...)            ──► Client      │
//        │                                               │
//        │   handlers = AgentHandlers{...}               │
//        │   on_initialize(...)          ◄── Client      │
//        │   on_session_new(...)         ◄── Client      │
//        │   on_session_prompt(...)      ◄── Client      │
//        │   on_session_cancel(...)      ◄── Client      │ notification
//        │   ...                                         │
//        └───────────────────────────────────────────────┘
//
#pragma once

#include <acp/rpc.hpp>

namespace acp {

//==============================================================================
//  AgentHandlers — the agent-side surface the client can call into.
//
//      Every required method is exposed; optional methods (load/resume/close/
//      delete/list, set_mode, set_config_option) are activated by setting the
//      corresponding handler.
//==============================================================================
struct AgentHandlers {
    // -------- required: initialize + new + prompt + cancel --------------
    std::function<InitializeResult (const InitializeParams&)>  on_initialize;
    std::function<NewSessionResult (const NewSessionParams&)>  on_session_new;
    std::function<PromptResult     (const PromptParams&)>      on_session_prompt;
    std::function<void             (const CancelParams&)>      on_session_cancel;

    // -------- optional --------------------------------------------------
    std::function<Unit                  (const AuthenticateParams&)>     on_authenticate;
    std::function<Unit                  ()>                              on_logout;

    std::function<Unit                  (const LoadSessionParams&)>      on_session_load;
    std::function<ResumeSessionResult   (const ResumeSessionParams&)>    on_session_resume;
    std::function<Unit                  (const CloseSessionParams&)>     on_session_close;
    std::function<Unit                  (const DeleteSessionParams&)>    on_session_delete;
    std::function<ListSessionsResult    (const ListSessionsParams&)>     on_session_list;

    std::function<Unit                    (const SetModeParams&)>          on_session_set_mode;
    std::function<SetConfigOptionResult   (const SetConfigOptionParams&)>  on_session_set_config_option;
};

//==============================================================================
//  ClientConnection — the agent's handle to the host editor.
//==============================================================================
class ClientConnection {
public:
    explicit ClientConnection(Transport sink, AgentHandlers handlers = {})
        : engine_(std::move(sink)) {
        install_handlers(std::move(handlers));
    }

    RpcEngine& engine() noexcept { return engine_; }

    // -------------------------------------------------------- outbound calls

    // The big one — Agent → Client streaming notifications.
    void session_update(const SessionUpdateMsg& m) {
        engine_.notify(method::SessionUpdate, m);
    }

    std::future<RequestPermissionResult> request_permission(const RequestPermissionParams& p) {
        return engine_.request<RequestPermissionResult>(method::RequestPermission, p);
    }

    std::future<ReadTextFileResult> fs_read_text_file(const ReadTextFileParams& p) {
        return engine_.request<ReadTextFileResult>(method::FsReadTextFile, p);
    }
    std::future<Unit> fs_write_text_file(const WriteTextFileParams& p) {
        return engine_.request<Unit>(method::FsWriteTextFile, p);
    }

    std::future<CreateTerminalResult> terminal_create(const CreateTerminalParams& p) {
        return engine_.request<CreateTerminalResult>(method::TerminalCreate, p);
    }
    std::future<TerminalOutputResult> terminal_output(const TerminalOutputParams& p) {
        return engine_.request<TerminalOutputResult>(method::TerminalOutput, p);
    }
    std::future<TerminalWaitForExitResult> terminal_wait_for_exit(const TerminalWaitForExitParams& p) {
        return engine_.request<TerminalWaitForExitResult>(method::TerminalWaitForExit, p);
    }
    std::future<Unit> terminal_kill(const TerminalKillParams& p) {
        return engine_.request<Unit>(method::TerminalKill, p);
    }
    std::future<Unit> terminal_release(const TerminalReleaseParams& p) {
        return engine_.request<Unit>(method::TerminalRelease, p);
    }

    // ----------------------------------------------------------- ext methods
    std::future<Json> ext_request(std::string_view m, const Json& p = Json::object()) {
        return engine_.ext_request(m, p);
    }
    void ext_notify(std::string_view m, const Json& p = Json::object()) {
        engine_.ext_notify(m, p);
    }

    // Cached negotiated capabilities (set after on_initialize fires, or any
    // time the agent wants to remember them).
    void remember_negotiated(const InitializeParams& p) noexcept { negotiated_ = p; }
    const Maybe<InitializeParams>& negotiated() const noexcept { return negotiated_; }

private:
    void install_handlers(AgentHandlers h) {
        // Required —
        if (h.on_initialize) {
            // Auto-cache client capabilities while we're here.
            engine_.on<InitializeParams, InitializeResult>(
                std::string(method::Initialize),
                [this, f = std::move(h.on_initialize)](const InitializeParams& p) {
                    remember_negotiated(p);
                    return f(p);
                });
        }
        if (h.on_session_new) {
            engine_.on<NewSessionParams, NewSessionResult>(
                std::string(method::SessionNew), std::move(h.on_session_new));
        }
        if (h.on_session_prompt) {
            engine_.on<PromptParams, PromptResult>(
                std::string(method::SessionPrompt), std::move(h.on_session_prompt));
        }
        if (h.on_session_cancel) {
            engine_.on_note<CancelParams>(
                std::string(method::SessionCancel), std::move(h.on_session_cancel));
        }

        // Optional —
        if (h.on_authenticate) {
            engine_.on<AuthenticateParams, Unit>(
                std::string(method::Authenticate), std::move(h.on_authenticate));
        }
        if (h.on_logout) {
            engine_.on<Unit, Unit>(std::string(method::Logout),
                [f = std::move(h.on_logout)](const Unit&) { return f(); });
        }
        if (h.on_session_load) {
            engine_.on<LoadSessionParams, Unit>(
                std::string(method::SessionLoad), std::move(h.on_session_load));
        }
        if (h.on_session_resume) {
            engine_.on<ResumeSessionParams, ResumeSessionResult>(
                std::string(method::SessionResume), std::move(h.on_session_resume));
        }
        if (h.on_session_close) {
            engine_.on<CloseSessionParams, Unit>(
                std::string(method::SessionClose), std::move(h.on_session_close));
        }
        if (h.on_session_delete) {
            engine_.on<DeleteSessionParams, Unit>(
                std::string(method::SessionDelete), std::move(h.on_session_delete));
        }
        if (h.on_session_list) {
            engine_.on<ListSessionsParams, ListSessionsResult>(
                std::string(method::SessionList), std::move(h.on_session_list));
        }
        if (h.on_session_set_mode) {
            engine_.on<SetModeParams, Unit>(
                std::string(method::SessionSetMode), std::move(h.on_session_set_mode));
        }
        if (h.on_session_set_config_option) {
            engine_.on<SetConfigOptionParams, SetConfigOptionResult>(
                std::string(method::SessionSetConfig), std::move(h.on_session_set_config_option));
        }
    }

    RpcEngine engine_;
    Maybe<InitializeParams> negotiated_;
};

} // namespace acp
