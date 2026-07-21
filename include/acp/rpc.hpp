// SPDX-License-Identifier: Apache-2.0
//
// acp/rpc.hpp — JSON-RPC 2.0, lifted into the codec algebra.
//
//   The wire is a tagged sum:
//
//     RpcMessage  ≅  Request      { id; method; params }
//                 +  Notification {     method; params }
//                 +  Response     { id; (result | error) }
//
//   (`result | error` is itself a sum; we model it as `Sum<Json, RpcError>`.)
//
//   The engine is two coroutine-like things glued by an in-flight table:
//
//     • inbound  : bytes →  RpcMessage  →  dispatch
//     • outbound : either notify(method, params)             (fire-and-forget)
//                    or    request(method, params) -> future<Json>
//
//   Transport is abstracted by a single function value:
//
//       using Transport = std::function<void(std::string_view utf8_no_newlines)>;
//
//   See acp/stdio.hpp for the canonical stdio transport.
//
#pragma once

#include <acp/methods.hpp>
#include <acp/codec.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>

namespace acp {

//==============================================================================
//  Error codes (standard JSON-RPC 2.0 + a custom slot for auth).
//==============================================================================
namespace errc {
inline constexpr int ParseError     = -32700;
inline constexpr int InvalidRequest = -32600;
inline constexpr int MethodNotFound = -32601;
inline constexpr int InvalidParams  = -32602;
inline constexpr int InternalError  = -32603;
inline constexpr int AuthRequired   = -32000;   // ACP convention
inline constexpr int Timeout        = -32001;   // request deadline exceeded
inline constexpr int Cancelled      = -32002;   // request cancelled locally
inline constexpr int ConnectionLost = -32003;   // transport closed mid-flight
} // namespace errc

//==============================================================================
//  RpcError — both an exception (thrown out of handlers / awaited futures) and
//             a value type (sent on the wire).
//==============================================================================
struct RpcError : std::runtime_error {
    int  code;
    Json data;
    RpcError(int c, std::string msg, Json d = Json())
        : std::runtime_error(std::move(msg)), code(c), data(std::move(d)) {}
};
template <> struct CodecOf<RpcError> {
    static Codec<RpcError> get() {
        return {
            [](const RpcError& e) -> Json {
                Json j = Json{{"code", e.code}, {"message", std::string(e.what())}};
                if (!e.data.is_null()) j["data"] = e.data;
                return j;
            },
            [](const Json& j) -> RpcError {
                int code = j.value("code", errc::InternalError);
                std::string msg = j.value("message", std::string{"rpc error"});
                Json data = j.value("data", Json());
                return RpcError(code, std::move(msg), std::move(data));
            }};
    }
};

//==============================================================================
//  Transport — an opaque sink that accepts a single UTF-8 string with NO
//              embedded newlines (per the stdio spec). The transport itself
//              is responsible for framing (e.g. appending '\n' before writing).
//==============================================================================
using Transport = std::function<void(std::string_view)>;

//==============================================================================
//  Observability hooks.
//
//      WireTrace   — called for every frame crossing the boundary, in both
//                    directions, with the raw JSON text. Wire it to a logger
//                    to debug a protocol exchange. Never throws into the engine
//                    (the engine swallows exceptions from the hook).
//
//      ErrorCallback — called when the engine detects a transport-level or
//                    dispatch-level fault that isn't tied to a single in-flight
//                    request (e.g. the reader thread hit EOF or an exception).
//                    A clean EOF reports errc::ConnectionLost with "eof".
//==============================================================================
enum class WireDir { Inbound, Outbound };
using WireTrace     = std::function<void(WireDir, std::string_view)>;
using ErrorCallback = std::function<void(int code, std::string_view message)>;

//==============================================================================
//  RpcEngine — bidirectional JSON-RPC dispatcher.
//
//      Handler registration is partitioned by whether the message has an id:
//
//        on_request(method, h)        h : Json → Json | throws RpcError
//        on_notification(method, h)   h : Json → ()   (any throw is swallowed)
//
//      Typed overloads use the codec algebra: the handler signature is
//
//        on<Params, Result>(method, h)             where h : Params → Result
//        on_note<Params>   (method, h)             where h : Params → ()
//
//      Outbound:
//
//        notify<Params>(method, params)            fire-and-forget
//        request<Result, Params>(method, params)   → std::future<Result>
//
//      Engine is thread-safe across handler registration, dispatch, and
//      outbound calls. A single reader thread should call `feed_line` for each
//      received frame; outbound calls may happen from any thread.
//==============================================================================
class RpcEngine {
public:
    explicit RpcEngine(Transport write) : write_(std::move(write)) {}

    ~RpcEngine() { stop_timer(); }

    RpcEngine(const RpcEngine&)            = delete;
    RpcEngine& operator=(const RpcEngine&) = delete;

    // ---------------------------------------------------------- observability
    // Install a wire tracer (every inbound/outbound frame) and/or an error
    // callback (transport faults not tied to one request). Both are optional.
    void set_wire_trace(WireTrace t)    { std::lock_guard lk(mu_); trace_ = std::move(t); has_trace_.store(static_cast<bool>(trace_), std::memory_order_release); }
    void set_error_callback(ErrorCallback e) { std::lock_guard lk(mu_); on_error_ = std::move(e); has_error_cb_.store(static_cast<bool>(on_error_), std::memory_order_release); }

    // Default deadline applied to every typed/raw request that doesn't pass an
    // explicit timeout. Zero (the default) means "wait forever".
    void set_default_timeout(std::chrono::milliseconds d) {
        default_timeout_.store(d.count(), std::memory_order_relaxed);
    }

    // ---------------------------------------------------------------- handlers
    //
    //   A request handler returns Maybe<Json>:
    //     • Just(result)  → the engine writes {result} synchronously (the
    //                        common, fully-synchronous case).
    //     • Nothing       → the handler took ownership of the reply and will
    //                        send it later from any thread via respond_raw()
    //                        / respond_error_raw() using the captured RpcId.
    //
    //   The deferred path is what lets a long-running handler (e.g. an agent
    //   driving a whole turn) hand the work to a worker thread WITHOUT blocking
    //   the reader thread — so the engine stays free to read the responses to
    //   any outbound requests (request_permission, fs/*, terminal/*) the turn
    //   makes. Blocking inline would deadlock a single-reader transport.
    using RawRequest      = std::function<Maybe<Json>(const RpcId&, const Json&)>;
    using RawNotification = std::function<void(const Json&)>;

    void on_request(std::string method, RawRequest h) {
        std::lock_guard lk(mu_);
        requests_[std::move(method)] = std::move(h);
    }
    void on_notification(std::string method, RawNotification h) {
        std::lock_guard lk(mu_);
        notifications_[std::move(method)] = std::move(h);
    }

    // Typed registrations: the codec algebra handles ser/de at the boundary.
    template <class Params, class Result, class F>
    void on(std::string method, F handler) {
        on_request(std::move(method),
            [h = std::move(handler)](const RpcId&, const Json& j) -> Maybe<Json> {
                Params p = j.is_null() ? Params{} : from_json<Params>(j);
                Result r = h(p);
                return Just<Json>(to_json(r));
            });
    }
    template <class Params, class F>
    void on_note(std::string method, F handler) {
        on_notification(std::move(method),
            [h = std::move(handler)](const Json& j) {
                Params p = j.is_null() ? Params{} : from_json<Params>(j);
                h(p);
            });
    }

    // ---------------------------------------------- generic request cancel
    //
    //   $/cancel_request (stabilized 2026-06-29). Register a handler that is
    //   invoked when the PEER asks to cancel an in-flight request by id. The
    //   handler receives the raw JSON-RPC id (number or string). Typical use:
    //   look the id up in an app-side table of running turns and signal that
    //   turn to stop. Registering also arms the engine's own bookkeeping so a
    //   cancel that targets an OUTBOUND request we are awaiting fails that
    //   future with errc::Cancelled immediately.
    //
    //   Safe to call once at wiring time. The engine owns the wire method
    //   registration; the app only supplies the semantic callback.
    void on_cancel_request(std::function<void(const RpcId&)> handler) {
        {
            std::lock_guard lk(mu_);
            cancel_handler_ = std::move(handler);
        }
        on_notification(std::string(method::CancelRequest),
            [this](const Json& j) {
                Json idj = j.is_object() ? j.value("id", Json()) : Json();
                if (idj.is_null()) return;
                // 1) If it targets an outbound request we're awaiting, fail it.
                if (fail_waiter_if_cancelled(idj)) {
                    // still fall through: the app may also track it
                }
                // 2) Hand the id to the app's cancel handler.
                std::function<void(const RpcId&)> h;
                { std::lock_guard lk(mu_); h = cancel_handler_; }
                if (h) { try { h(idj); } catch (...) {} }
            });
    }

    // Outbound: ask the peer to cancel the request with this id. `id` is the
    // JSON-RPC id returned/observed for the request being cancelled.
    void notify_cancel_request(const RpcId& id) {
        notify_raw(method::CancelRequest, Json{{"id", id}});
    }

    // -------------------------------------------------- deferred (async) replies
    //
    //   Responder<Result> — a one-shot handle to a pending inbound request. The
    //   async handler captures it, hands it to a worker, and the worker calls
    //   .ok(result) or .error(...) when the work completes. Safe to move; the
    //   reply is sent exactly once (subsequent calls are no-ops).
    template <class Result>
    class Responder {
    public:
        Responder(RpcEngine* e, RpcId id) : eng_(e), id_(std::move(id)) {}
        Responder(Responder&& o) noexcept
            : eng_(o.eng_), id_(std::move(o.id_)), done_(o.done_) { o.eng_ = nullptr; }
        Responder& operator=(Responder&&) = delete;
        Responder(const Responder&)        = delete;
        Responder& operator=(const Responder&) = delete;

        void ok(const Result& r) {
            if (!eng_ || done_) return;
            done_ = true;
            if constexpr (std::is_same_v<Result, Unit>)
                eng_->respond_raw(id_, Json::object());
            else
                eng_->respond_raw(id_, to_json(r));
        }
        void error(int code, std::string message, Json data = Json()) {
            if (!eng_ || done_) return;
            done_ = true;
            eng_->respond_error_raw(id_, code, std::move(message), std::move(data));
        }
        void error(const RpcError& e) { error(e.code, e.what(), e.data); }
        const RpcId& id() const noexcept { return id_; }

    private:
        RpcEngine* eng_;
        RpcId      id_;
        bool       done_ = false;
    };

    // Register a handler that answers asynchronously. The handler runs on the
    // reader thread but returns immediately (void); it must move the Responder
    // somewhere that eventually calls .ok()/.error(). The engine writes NO
    // synchronous reply for this method.
    template <class Params, class Result, class F>
    void on_async(std::string method, F handler) {
        on_request(std::move(method),
            [this, h = std::move(handler)](const RpcId& id, const Json& j) -> Maybe<Json> {
                Params p = j.is_null() ? Params{} : from_json<Params>(j);
                h(p, Responder<Result>(this, id));
                return Nothing;   // deferred — reply travels later
            });
    }

    // Send a late reply to a previously-deferred request. Thread-safe; the
    // write is serialised through the transport's sink.
    void respond_raw(const RpcId& id, const Json& result) {
        write_line(build_response(id, result));
    }
    void respond_error_raw(const RpcId& id, int code, std::string message,
                           Json data = Json()) {
        send_error(id, code, std::move(message), std::move(data));
    }

    // ---------------------------------------------------------------- outbound
    // Raw (Json) notification — for custom/extension methods.
    //
    //   HOT PATH. session/update fires this once per streamed text delta and
    //   once per tool-call transition — hundreds of times per turn. We do NOT
    //   build an intermediate `{jsonrpc,method,params}` Json object and then
    //   dump() it: that allocates a second object node + three map entries and
    //   re-walks the params tree. Instead we serialize the fixed envelope
    //   prefix as raw bytes and let params.dump() write straight into the same
    //   reused string via nlohmann's SAX-free dump-into-buffer.
    void notify_raw(std::string_view method, const Json& params) {
        write_line(build_notification(method, params));
    }
    // Move overload — params is consumed; identical wire output. Kept distinct
    // so callers that own an rvalue Json (typed notify) don't force a copy.
    void notify_raw(std::string_view method, Json&& params) {
        write_line(build_notification(method, params));
    }
    // Typed notification.
    template <class Params>
    void notify(std::string_view method, const Params& params) {
        notify_raw(method, to_json(params));   // to_json returns an rvalue Json → move overload
    }
    // Notification with no params (e.g. `logout` is technically a request, but
    // many extension notifications carry no payload).
    void notify(std::string_view method) { notify_raw(method, Json::object()); }

    // Raw request — returns the future as raw Json.
    //
    //   `timeout` of zero means "use the engine default" (set_default_timeout);
    //   if that is also zero the request waits forever. A non-zero timeout that
    //   elapses before a response fails the future with RpcError(errc::Timeout).
    //
    //   The returned future is backed DIRECTLY by the promise the reader thread
    //   fulfils — not a deferred std::async — so wait_for / wait_until behave
    //   correctly (a deferred future would report `deferred` and never block).
    std::future<Json> request_raw(std::string_view method, const Json& params,
                                  std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) {
        std::int64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

        auto promise = std::make_shared<std::promise<Json>>();
        std::future<Json> fut = promise->get_future();

        long long ms = timeout.count();
        if (ms == 0) ms = default_timeout_.load(std::memory_order_relaxed);
        const bool has_deadline = ms > 0;
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(ms);
        {
            std::lock_guard lk(mu_);
            // Waiter keyed on the RAW integer id we just minted — no id.dump()
            // string allocation per request. Every id we send is an integer,
            // so the echoed response id parses back to the same key.
            Waiter w;
            w.promise      = std::move(promise);
            w.has_deadline = has_deadline;
            w.deadline     = deadline;
            waiters_.emplace(id, std::move(w));
        }
        if (has_deadline) ensure_timer();

        Json env = {{"jsonrpc", "2.0"}, {"id", id}, {"method", std::string(method)}};
        if (!params.is_null()) env["params"] = params;
        write_line(env.dump());
        if (has_deadline) timer_cv_.notify_all();
        return fut;
    }
    // Typed request : Params → future<Result>.
    template <class Result, class Params>
    std::future<Result> request(std::string_view method, const Params& params,
                                std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) {
        auto raw = std::make_shared<std::future<Json>>(
            request_raw(method, to_json(params), timeout));
        // A deferred wrapper here is fine: it only performs the cheap decode
        // step; the actual blocking happens inside raw->get(), which is backed
        // by a real promise (so timed waits on the wrapper still work via the
        // underlying shared state once .get() is reached).
        return std::async(std::launch::deferred,
            [raw]() -> Result {
                Json j = raw->get();
                if constexpr (std::is_same_v<Result, Unit>) return Unit{};
                else return j.is_null() ? Result{} : from_json<Result>(j);
            });
    }
    // Result-only typed request (no params).
    template <class Result>
    std::future<Result> request(std::string_view method,
                                std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) {
        return request<Result, Unit>(method, Unit{}, timeout);
    }

    // ------------------------------------------------------------- ext methods
    //
    //   ExtRequest / ExtNotification escape hatches — send/receive any custom
    //   method that is not part of the spec. Convention: method names starting
    //   with "_" are reserved for ext use.
    //
    //   These are thin wrappers over request_raw / notify_raw / on_request /
    //   on_notification, but they make the intent explicit at call sites.
    //
    std::future<Json> ext_request(std::string_view method, const Json& params = Json::object()) {
        return request_raw(method, params);
    }
    void ext_notify(std::string_view method, const Json& params = Json::object()) {
        notify_raw(method, params);
    }
    void on_ext_request(std::string method, std::function<Json(const Json&)> h) {
        on_request(std::move(method),
            [h = std::move(h)](const RpcId&, const Json& p) -> Maybe<Json> {
                return Just<Json>(h(p));
            });
    }
    void on_ext_notification(std::string method, std::function<void(const Json&)> h) {
        on_notification(std::move(method), std::move(h));
    }

    // -------------------------------------------------------------- inbound
    // Feed a single received line (one JSON-RPC envelope, exactly).
    void feed_line(std::string_view line) {
        if (line.empty()) return;
        emit_trace(WireDir::Inbound, line);
        Json msg;
        try {
            msg = Json::parse(line);
        } catch (const std::exception& e) {
            send_error(Json(nullptr), errc::ParseError, e.what());
            return;
        }
        if (msg.is_array()) {
            for (const auto& m : msg) dispatch_one(m);
        } else {
            dispatch_one(msg);
        }
    }

    // Report a transport-level fault (EOF, reader exception). The supplied
    // transports call this when the read pump stops. Fails every in-flight
    // request and notifies the error callback. Idempotent in effect.
    void on_transport_closed(std::string reason = "connection closed") {
        report_error(errc::ConnectionLost, reason);
        shutdown(std::move(reason), errc::ConnectionLost);
    }

    // ------------------------------------------------------------- lifecycle
    // Cancel every outstanding outbound request with an error. Idempotent.
    void shutdown(std::string reason = "engine shutdown",
                  int code = errc::InternalError) {
        std::unordered_map<std::int64_t, Waiter> taken;
        {
            std::lock_guard lk(mu_);
            taken.swap(waiters_);
        }
        for (auto& [id, w] : taken) {
            if (!w.promise) continue;
            try {
                w.promise->set_exception(std::make_exception_ptr(
                    RpcError(code, reason)));
            } catch (...) {}
        }
        stop_timer();
    }

private:
    // ----------------------------------------------------------- bookkeeping

    // Append `s` as a JSON string literal (with surrounding quotes) to `out`.
    // Method names are ASCII spec constants, but we escape defensively so a
    // caller-supplied ext method can never corrupt the frame.
    static void append_json_string(std::string& out, std::string_view s) {
        out.push_back('"');
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        static const char* hex = "0123456789abcdef";
                        out += "\\u00";
                        out.push_back(hex[(c >> 4) & 0xF]);
                        out.push_back(hex[c & 0xF]);
                    } else {
                        out.push_back(c);
                    }
            }
        }
        out.push_back('"');
    }

    // Serialize a JSON-RPC notification envelope directly into a string,
    // dumping `params` inline instead of building an intermediate Json object.
    // Equivalent wire bytes to `{{"jsonrpc","2.0"},{"method",m},{"params",p}}`
    // .dump() but with one fewer object allocation and no map inserts.
    static std::string build_notification(std::string_view method, const Json& params) {
        std::string out;
        out.reserve(48);
        out += "{\"jsonrpc\":\"2.0\",\"method\":";
        append_json_string(out, method);
        if (!params.is_null()) {
            out += ",\"params\":";
            out += params.dump(-1, ' ', false, Json::error_handler_t::replace);
        }
        out.push_back('}');
        return out;
    }

    // Serialize a successful JSON-RPC response envelope directly. `id` is a
    // small int or short string, `result` dumped inline. Same win as
    // build_notification: no intermediate `{jsonrpc,id,result}` object.
    static std::string build_response(const Json& id, const Json& result) {
        std::string out;
        out.reserve(48);
        out += "{\"jsonrpc\":\"2.0\",\"id\":";
        out += id.dump(-1, ' ', false, Json::error_handler_t::replace);
        out += ",\"result\":";
        out += (result.is_null() ? std::string("null")
                                 : result.dump(-1, ' ', false, Json::error_handler_t::replace));
        out.push_back('}');
        return out;
    }

    void write_line(std::string s) {
        // Spec: each frame is one JSON-RPC envelope with NO embedded newlines.
        // nlohmann::json::dump() never inserts literal '\n' (we don't pass
        // pretty-print). The transport appends framing.
        emit_trace(WireDir::Outbound, s);
        if (write_) write_(s);
    }

    void send_error(const RpcId& id, int code, std::string msg, Json data = Json()) {
        Json env = {{"jsonrpc", "2.0"}, {"id", id}};
        Json err = {{"code", code}, {"message", std::move(msg)}};
        if (!data.is_null()) err["data"] = std::move(data);
        env["error"] = std::move(err);
        write_line(env.dump());
    }

    void dispatch_one(const Json& msg) {
        if (!msg.is_object() || msg.value("jsonrpc", "") != "2.0") {
            send_error(msg.value("id", Json(nullptr)), errc::InvalidRequest,
                       "missing or invalid 'jsonrpc' field");
            return;
        }
        const bool has_method = msg.contains("method");
        const bool has_id     = msg.contains("id");
        const bool has_result = msg.contains("result");
        const bool has_error  = msg.contains("error");

        if (has_method && has_id)                return handle_request(msg);
        if (has_method && !has_id)               return handle_notification(msg);
        if (has_id && (has_result || has_error)) return handle_response(msg);
        send_error(msg.value("id", Json(nullptr)), errc::InvalidRequest,
                   "envelope is neither request, notification, nor response");
    }

    void handle_request(const Json& msg) {
        const auto& method = msg.at("method").get_ref<const std::string&>();
        const RpcId id     = msg.at("id");
        // Reference the existing params subtree instead of deep-copying it out
        // of the parsed envelope — `msg` outlives this call and the handler only
        // reads params. A prompt's params tree can be large; the copy was pure
        // waste on every inbound request.
        static const Json kEmptyObject = Json::object();
        auto pit = msg.find("params");
        const Json& params = (pit != msg.end()) ? *pit : kEmptyObject;

        RawRequest h;
        {
            std::lock_guard lk(mu_);
            if (auto it = requests_.find(method); it != requests_.end()) h = it->second;
        }
        if (!h) {
            return send_error(id, errc::MethodNotFound, "Method not found: " + method);
        }
        Maybe<Json> result;
        try {
            result = h(id, params);
        } catch (const RpcError& e) {
            return send_error(id, e.code, e.what(), e.data);
        } catch (const CodecError& e) {
            return send_error(id, errc::InvalidParams, e.what());
        } catch (const std::exception& e) {
            return send_error(id, errc::InternalError, e.what());
        } catch (...) {
            return send_error(id, errc::InternalError, "unknown exception");
        }
        // Nothing ⇒ the handler deferred; it will send the reply itself later.
        if (!result) return;
        write_line(build_response(id, *result));
    }

    void handle_notification(const Json& msg) {
        const auto& method = msg.at("method").get_ref<const std::string&>();
        static const Json kEmptyObject = Json::object();
        auto pit = msg.find("params");
        const Json& params = (pit != msg.end()) ? *pit : kEmptyObject;

        RawNotification h;
        {
            std::lock_guard lk(mu_);
            if (auto it = notifications_.find(method); it != notifications_.end()) h = it->second;
        }
        if (!h) return;        // unknown notifications: ignore silently per spec
        try { h(params); } catch (...) { /* notifications never respond */ }
    }

    // If `idj` names an OUTBOUND request we are currently awaiting, fail its
    // future with errc::Cancelled and remove the waiter. Returns true if a
    // waiter was found and cancelled. Used by the $/cancel_request handler so
    // that a peer cancelling a request we sent unblocks the caller promptly
    // instead of waiting for the deadline.
    bool fail_waiter_if_cancelled(const Json& idj) {
        std::int64_t key;
        if (idj.is_number_integer())        key = idj.get<std::int64_t>();
        else if (idj.is_number_unsigned())  key = static_cast<std::int64_t>(idj.get<std::uint64_t>());
        else return false;   // not one of our integer ids

        std::shared_ptr<std::promise<Json>> p;
        {
            std::lock_guard lk(mu_);
            if (auto it = waiters_.find(key); it != waiters_.end()) {
                p = std::move(it->second.promise);
                waiters_.erase(it);
            }
        }
        if (!p) return false;
        try {
            p->set_exception(std::make_exception_ptr(
                RpcError(errc::Cancelled, "request cancelled by peer")));
        } catch (...) {}
        return true;
    }

    void handle_response(const Json& msg) {
        // We only ever mint integer ids for our outbound requests, so a
        // conforming peer echoes an integer id. Extract it directly — no
        // id.dump() string allocation on the response hot path. A non-integer
        // id here is a peer bug (or a response to an id we never sent); we drop
        // it rather than allocate a coercion string.
        const Json& idj = msg.at("id");
        std::int64_t key;
        if (idj.is_number_integer())        key = idj.get<std::int64_t>();
        else if (idj.is_number_unsigned())  key = static_cast<std::int64_t>(idj.get<std::uint64_t>());
        else return;   // string / null / float id: not one of ours

        std::shared_ptr<std::promise<Json>> p;
        {
            std::lock_guard lk(mu_);
            if (auto it = waiters_.find(key); it != waiters_.end()) {
                p = std::move(it->second.promise);
                waiters_.erase(it);
            }
        }
        if (!p) return;   // dropped: unknown / already-resolved id

        try {
            if (msg.contains("error")) {
                p->set_exception(std::make_exception_ptr(
                    from_json<RpcError>(msg.at("error"))));
            } else {
                p->set_value(msg.value("result", Json()));
            }
        } catch (const std::future_error&) { /* already satisfied */ }
    }

    // ----------------------------------------------------------- hooks/timer
    void emit_trace(WireDir dir, std::string_view frame) {
        // Fast path: no tracer installed (the production default) — skip the
        // mutex entirely. This runs on every inbound AND outbound frame.
        if (!has_trace_.load(std::memory_order_acquire)) return;
        WireTrace t;
        { std::lock_guard lk(mu_); t = trace_; }
        if (t) { try { t(dir, frame); } catch (...) {} }
    }
    void report_error(int code, std::string_view msg) {
        if (!has_error_cb_.load(std::memory_order_acquire)) return;
        ErrorCallback e;
        { std::lock_guard lk(mu_); e = on_error_; }
        if (e) { try { e(code, msg); } catch (...) {} }
    }

    // Start the deadline-monitor thread once, on the first timed request.
    void ensure_timer() {
        bool expected = false;
        if (!timer_started_.compare_exchange_strong(expected, true,
                                                    std::memory_order_acq_rel))
            return;   // already started
        timer_running_.store(true, std::memory_order_release);
        timer_thread_ = std::thread([this] { timer_loop(); });
    }

    void stop_timer() {
        if (!timer_started_.load(std::memory_order_acquire)) return;
        timer_running_.store(false, std::memory_order_release);
        timer_cv_.notify_all();
        if (timer_thread_.joinable() &&
            timer_thread_.get_id() != std::this_thread::get_id())
            timer_thread_.join();
    }

    // Wakes on the nearest deadline; fails any waiter whose deadline passed.
    void timer_loop() {
        std::unique_lock lk(mu_);
        while (timer_running_.load(std::memory_order_acquire)) {
            auto now = std::chrono::steady_clock::now();
            auto next = std::chrono::steady_clock::time_point::max();
            bool any = false;

            for (auto it = waiters_.begin(); it != waiters_.end(); ) {
                auto& w = it->second;
                if (!w.has_deadline) { ++it; continue; }
                if (w.deadline <= now) {
                    auto p = std::move(w.promise);
                    it = waiters_.erase(it);
                    if (p) {
                        try {
                            p->set_exception(std::make_exception_ptr(
                                RpcError(errc::Timeout, "request timed out")));
                        } catch (...) {}
                    }
                } else {
                    any = true;
                    if (w.deadline < next) next = w.deadline;
                    ++it;
                }
            }

            if (any) timer_cv_.wait_until(lk, next);
            else     timer_cv_.wait(lk);
        }
    }

    Transport write_;
    std::atomic<std::int64_t> next_id_{1};
    std::mutex mu_;
    std::unordered_map<std::string, RawRequest>      requests_;
    std::unordered_map<std::string, RawNotification> notifications_;

    // A pending outbound request: its promise plus an optional deadline. Keyed
    // by the RAW integer JSON-RPC id we minted in request_raw — no per-request
    // string allocation. Non-integer response ids (a peer bug) are dropped in
    // handle_response rather than being looked up here.
    struct Waiter {
        std::shared_ptr<std::promise<Json>> promise;
        std::chrono::steady_clock::time_point deadline{};   // zero == none
        bool has_deadline = false;
        bool done = false;
    };
    std::unordered_map<std::int64_t, Waiter> waiters_;

    // App callback for peer-initiated $/cancel_request (set via on_cancel_request).
    std::function<void(const RpcId&)> cancel_handler_;

    WireTrace     trace_;
    ErrorCallback on_error_;
    std::atomic<bool> has_trace_{false};
    std::atomic<bool> has_error_cb_{false};
    std::atomic<long long> default_timeout_{0};   // ms; 0 == no timeout

    // Deadline monitor — a lazily-started background thread that fails any
    // waiter whose deadline has passed. Started on the first timed request.
    std::thread             timer_thread_;
    std::condition_variable timer_cv_;
    std::atomic<bool>       timer_running_{false};
    std::atomic<bool>       timer_started_{false};
};

} // namespace acp
