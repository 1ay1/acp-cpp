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
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
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

    // ---------------------------------------------------------------- handlers
    using RawRequest      = std::function<Json(const Json&)>;
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
            [h = std::move(handler)](const Json& j) -> Json {
                Params p = j.is_null() ? Params{} : from_json<Params>(j);
                Result r = h(p);
                return to_json(r);
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

    // ---------------------------------------------------------------- outbound
    // Raw (Json) notification — for custom/extension methods.
    void notify_raw(std::string_view method, const Json& params) {
        Json env = {{"jsonrpc", "2.0"}, {"method", std::string(method)}};
        if (!params.is_null()) env["params"] = params;
        write_line(env.dump());
    }
    // Typed notification.
    template <class Params>
    void notify(std::string_view method, const Params& params) {
        notify_raw(method, to_json(params));
    }
    // Notification with no params (e.g. `logout` is technically a request, but
    // many extension notifications carry no payload).
    void notify(std::string_view method) { notify_raw(method, Json::object()); }

    // Raw request — returns the future as raw Json.
    std::future<Json> request_raw(std::string_view method, const Json& params) {
        std::int64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
        auto promise = std::make_shared<std::promise<Json>>();
        std::future<Json> fut = promise->get_future();
        {
            std::lock_guard lk(mu_);
            waiters_.emplace(id, std::move(promise));
        }
        Json env = {{"jsonrpc", "2.0"}, {"id", id}, {"method", std::string(method)}};
        if (!params.is_null()) env["params"] = params;
        write_line(env.dump());
        return fut;
    }
    // Typed request : Params → future<Result>.
    template <class Result, class Params>
    std::future<Result> request(std::string_view method, const Params& params) {
        auto raw = request_raw(method, to_json(params));
        return std::async(std::launch::deferred,
            [r = std::move(raw)]() mutable -> Result {
                Json j = r.get();
                if constexpr (std::is_same_v<Result, Unit>) return Unit{};
                else return j.is_null() ? Result{} : from_json<Result>(j);
            });
    }
    // Result-only typed request (no params).
    template <class Result>
    std::future<Result> request(std::string_view method) {
        return request<Result, Unit>(method, Unit{});
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
        on_request(std::move(method), std::move(h));
    }
    void on_ext_notification(std::string method, std::function<void(const Json&)> h) {
        on_notification(std::move(method), std::move(h));
    }

    // -------------------------------------------------------------- inbound
    // Feed a single received line (one JSON-RPC envelope, exactly).
    void feed_line(std::string_view line) {
        if (line.empty()) return;
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

    // ------------------------------------------------------------- lifecycle
    // Cancel every outstanding outbound request with an error. Idempotent.
    void shutdown(std::string reason = "engine shutdown") {
        std::unordered_map<std::int64_t, std::shared_ptr<std::promise<Json>>> taken;
        {
            std::lock_guard lk(mu_);
            taken.swap(waiters_);
        }
        for (auto& [id, p] : taken) {
            try {
                p->set_exception(std::make_exception_ptr(
                    RpcError(errc::InternalError, reason)));
            } catch (...) {}
        }
    }

private:
    // ----------------------------------------------------------- bookkeeping
    void write_line(std::string s) {
        // Spec: each frame is one JSON-RPC envelope with NO embedded newlines.
        // nlohmann::json::dump() never inserts literal '\n' (we don't pass
        // pretty-print). The transport appends framing.
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
        const Json params  = msg.value("params", Json::object());

        RawRequest h;
        {
            std::lock_guard lk(mu_);
            if (auto it = requests_.find(method); it != requests_.end()) h = it->second;
        }
        if (!h) {
            return send_error(id, errc::MethodNotFound, "Method not found: " + method);
        }
        Json result;
        try {
            result = h(params);
        } catch (const RpcError& e) {
            return send_error(id, e.code, e.what(), e.data);
        } catch (const CodecError& e) {
            return send_error(id, errc::InvalidParams, e.what());
        } catch (const std::exception& e) {
            return send_error(id, errc::InternalError, e.what());
        } catch (...) {
            return send_error(id, errc::InternalError, "unknown exception");
        }
        Json env = {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
        write_line(env.dump());
    }

    void handle_notification(const Json& msg) {
        const auto& method = msg.at("method").get_ref<const std::string&>();
        const Json params  = msg.value("params", Json::object());

        RawNotification h;
        {
            std::lock_guard lk(mu_);
            if (auto it = notifications_.find(method); it != notifications_.end()) h = it->second;
        }
        if (!h) return;        // unknown notifications: ignore silently per spec
        try { h(params); } catch (...) { /* notifications never respond */ }
    }

    void handle_response(const Json& msg) {
        // We always serialise id as int64; accept string ids by coercion.
        std::int64_t key = 0;
        const auto& id = msg.at("id");
        if (id.is_number_integer()) key = id.get<std::int64_t>();
        else if (id.is_string()) {
            try { key = std::stoll(id.get<std::string>()); } catch (...) { return; }
        } else return;

        std::shared_ptr<std::promise<Json>> p;
        {
            std::lock_guard lk(mu_);
            if (auto it = waiters_.find(key); it != waiters_.end()) {
                p = std::move(it->second);
                waiters_.erase(it);
            }
        }
        if (!p) return;   // dropped: unknown id

        try {
            if (msg.contains("error")) {
                p->set_exception(std::make_exception_ptr(
                    from_json<RpcError>(msg.at("error"))));
            } else {
                p->set_value(msg.value("result", Json()));
            }
        } catch (const std::future_error&) { /* already satisfied */ }
    }

    Transport write_;
    std::atomic<std::int64_t> next_id_{1};
    std::mutex mu_;
    std::unordered_map<std::string, RawRequest>      requests_;
    std::unordered_map<std::string, RawNotification> notifications_;
    std::unordered_map<std::int64_t, std::shared_ptr<std::promise<Json>>> waiters_;
};

} // namespace acp
