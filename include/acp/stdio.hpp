// SPDX-License-Identifier: Apache-2.0
//
// acp/stdio.hpp — line-delimited stdio transport (fast path).
//
//   Per the ACP spec:
//     • messages are JSON-RPC envelopes
//     • framing is a single '\n' between messages
//     • messages MUST NOT contain embedded '\n'
//     • stderr is free for logging (we leave it alone)
//
//   There are two transports here:
//
//     FdTransport   — the FAST, default path. Talks to raw file descriptors
//                     with read(2)/writev(2). No std::iostream in the hot loop:
//                       • write side frames payload+'\n' in a single writev
//                         (one syscall per frame), guarded by a write mutex;
//                       • an opt-in COALESCING buffer batches frames emitted
//                         back-to-back into one writev + one flush, which is
//                         the dominant win for a streaming agent that emits
//                         many session/update notifications per turn;
//                       • read side pumps a reusable buffer with read(2) and a
//                         persistent partial-line accumulator — no per-line
//                         heap churn, no sync-with-stdio penalty.
//
//     StdioTransport — the portable std::istream/std::ostream path, kept for
//                      callers that must wrap arbitrary C++ streams (tests,
//                      in-memory loopback). Correct but slower.
//
//   Both stop their reader on EOF or stop(); on natural EOF the engine's
//   on_transport_closed() fires, failing all in-flight requests.
//
#pragma once

#include <acp/rpc.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <istream>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <io.h>
#  include <windows.h>
#else
#  include <sys/uio.h>
#  include <unistd.h>
#endif

namespace acp {

//==============================================================================
//  FdTransport — raw file-descriptor transport. This is the default and the
//  fast path. Construct it from the process's stdin/stdout fds (or any pair of
//  fds, e.g. a spawned child's pipes) and it does zero std::iostream work.
//==============================================================================
class FdTransport {
public:
    // in_fd  — descriptor to READ JSON-RPC frames from (our stdin, or a child's stdout)
    // out_fd — descriptor to WRITE JSON-RPC frames to  (our stdout, or a child's stdin)
    FdTransport(int in_fd, int out_fd) : in_fd_(in_fd), out_fd_(out_fd) {}

    // The canonical process transport: stdin (fd 0) → stdout (fd 1).
    static FdTransport process() { return FdTransport(0, 1); }

    FdTransport(const FdTransport&)            = delete;
    FdTransport& operator=(const FdTransport&) = delete;

    ~FdTransport() { stop(); }

    // ---------------------------------------------------------------- write
    //
    //   The Transport function the engine writes through. When not batching,
    //   each call frames `payload + '\n'` and pushes it out in ONE writev.
    //   When a batch is open (begin_batch), frames accumulate into an internal
    //   buffer and are flushed as a single writev by end_batch().
    Transport sink() {
        return [this](std::string_view line) { emit(line); };
    }

    // Open a coalescing batch on the CURRENT thread. Every frame written by
    // THIS thread while the batch is open is buffered; end_batch() flushes them
    // in one syscall. Batching is thread-local: a batch opened on the reader
    // thread (e.g. replay_history) never traps a live streaming chunk emitted
    // concurrently by a worker turn thread — that chunk goes out immediately.
    // Reentrant-safe via a per-thread depth counter so nested begin/end pairs
    // collapse to the outermost flush.
    void begin_batch() {
        ++tl_batch().depth;
    }
    void end_batch() {
        TlBatch& b = tl_batch();
        if (b.depth == 0) return;
        if (--b.depth == 0 && !b.buf.empty()) {
            std::lock_guard lk(write_mu_);
            write_all_locked(b.buf.data(), b.buf.size());
            b.buf.clear();
        }
    }

    // RAII batch guard — coalesces every frame emitted during its lifetime.
    class Batch {
    public:
        explicit Batch(FdTransport& t) : t_(&t) { t_->begin_batch(); }
        Batch(Batch&& o) noexcept : t_(o.t_) { o.t_ = nullptr; }
        Batch& operator=(Batch&&) = delete;
        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;
        ~Batch() { if (t_) t_->end_batch(); }
    private:
        FdTransport* t_;
    };
    [[nodiscard]] Batch batch() { return Batch(*this); }

    // ---------------------------------------------------------------- read
    void start(RpcEngine& engine) {
        engine_ = &engine;
        running_.store(true, std::memory_order_release);
        reader_ = std::thread([this, &engine] { read_loop(engine); });
    }

    // Block until the reader thread finishes (i.e. until EOF on the input fd).
    void join() {
        if (reader_.joinable() &&
            reader_.get_id() != std::this_thread::get_id())
            reader_.join();
    }

    void stop() {
        running_.store(false, std::memory_order_release);
        // We cannot portably interrupt a blocking read(2); the peer closing the
        // descriptor (EOF) wakes the reader. join() does the rest.
        if (reader_.joinable() &&
            reader_.get_id() != std::this_thread::get_id())
            reader_.join();
    }

    bool running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    // Per-thread coalescing state: a buffer + open-batch depth. Thread-local so
    // batches on different threads never interfere and the buffer needs no
    // lock while it fills; only the final writev takes write_mu_.
    struct TlBatch { std::string buf; unsigned depth = 0; };
    static TlBatch& tl_batch() { thread_local TlBatch b; return b; }

    // Emit one frame: buffer it if THIS thread has a batch open, else writev now.
    void emit(std::string_view line) {
        TlBatch& b = tl_batch();
        if (b.depth > 0) {
            b.buf.append(line.data(), line.size());
            b.buf.push_back('\n');
            return;
        }
        std::lock_guard lk(write_mu_);
        write_frame_locked(line);
    }

    // One frame → one writev(payload, "\n"). Falls back to write_all on
    // platforms without writev (Windows).
    void write_frame_locked(std::string_view line) {
#if defined(_WIN32)
        write_all_locked(line.data(), line.size());
        const char nl = '\n';
        write_all_locked(&nl, 1);
#else
        const char nl = '\n';
        struct iovec iov[2];
        iov[0].iov_base = const_cast<char*>(line.data());
        iov[0].iov_len  = line.size();
        iov[1].iov_base = const_cast<char*>(&nl);
        iov[1].iov_len  = 1;
        std::size_t total = line.size() + 1;
        std::size_t done  = 0;
        // Handle partial writev by retrying the remaining bytes with write.
        while (done < total) {
            if (done == 0) {
                ssize_t n = ::writev(out_fd_, iov, 2);
                if (n < 0) { if (errno == EINTR) continue; return; }
                done += static_cast<std::size_t>(n);
            } else if (done < line.size()) {
                write_tail_locked(line.data() + done, line.size() - done);
                write_tail_locked(&nl, 1);
                done = total;
            } else {
                done = total;  // only the '\n' remained and it went out above
            }
        }
#endif
    }

    // Robust write of a contiguous buffer (loops over short writes / EINTR).
    void write_all_locked(const char* data, std::size_t len) {
        std::size_t off = 0;
        while (off < len) {
#if defined(_WIN32)
            int n = ::_write(out_fd_, data + off, static_cast<unsigned>(len - off));
#else
            ssize_t n = ::write(out_fd_, data + off, len - off);
#endif
            if (n < 0) {
#if !defined(_WIN32)
                if (errno == EINTR) continue;
#endif
                return;  // fd broken; drop (engine will notice on read EOF)
            }
            if (n == 0) return;
            off += static_cast<std::size_t>(n);
        }
    }
    void write_tail_locked(const char* data, std::size_t len) {
        write_all_locked(data, len);
    }

    // Raw read pump: read(2) into a slab, split on '\n', accumulate partials.
    void read_loop(RpcEngine& engine) {
        constexpr std::size_t kChunk = 64 * 1024;
        std::vector<char> slab(kChunk);
        std::string partial;   // bytes of an unfinished line carried across reads

        while (running_.load(std::memory_order_acquire)) {
#if defined(_WIN32)
            int n = ::_read(in_fd_, slab.data(), static_cast<unsigned>(slab.size()));
#else
            ssize_t n = ::read(in_fd_, slab.data(), slab.size());
#endif
            if (n < 0) {
#if !defined(_WIN32)
                if (errno == EINTR) continue;
#endif
                break;   // read error
            }
            if (n == 0) break;   // EOF

            const char* p   = slab.data();
            const char* end = p + n;
            while (p < end) {
                const char* nl = static_cast<const char*>(
                    std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
                if (!nl) {
                    partial.append(p, static_cast<std::size_t>(end - p));
                    break;
                }
                std::string_view frame;
                if (partial.empty()) {
                    frame = std::string_view(p, static_cast<std::size_t>(nl - p));
                    deliver(engine, frame);
                } else {
                    partial.append(p, static_cast<std::size_t>(nl - p));
                    deliver(engine, partial);
                    partial.clear();
                }
                p = nl + 1;
            }
        }
        const bool was_running = running_.exchange(false, std::memory_order_acq_rel);
        if (was_running) engine.on_transport_closed("eof");
    }

    static void deliver(RpcEngine& engine, std::string_view frame) {
        if (frame.empty()) return;
        // Tolerate CRLF just in case a peer frames with '\r\n'.
        if (frame.back() == '\r') frame.remove_suffix(1);
        if (frame.empty()) return;
        try { engine.feed_line(frame); } catch (...) { /* never kill the pump */ }
    }

    int  in_fd_;
    int  out_fd_;
    std::mutex   write_mu_;        // serialises the actual write(2)/writev(2)
    std::thread  reader_;
    std::atomic<bool> running_{false};
    RpcEngine*   engine_{nullptr};
};

//==============================================================================
//  StdioTransport — portable std::istream/std::ostream transport. Kept for
//  callers that must wrap arbitrary C++ streams (in-memory loopback, tests).
//  Correct, but does per-message flush + std::getline; prefer FdTransport for
//  a real process boundary.
//==============================================================================
class StdioTransport {
public:
    // The streams must outlive the transport. `in` is the agent's stdin (when
    // wrapping an agent) or the spawned child's stdout (when wrapping a client).
    StdioTransport(std::istream& in, std::ostream& out)
        : in_(in), out_(out) {}

    StdioTransport(const StdioTransport&)            = delete;
    StdioTransport& operator=(const StdioTransport&) = delete;

    ~StdioTransport() { stop(); }

    // The Transport function the engine writes through.
    Transport sink() {
        return [this](std::string_view line) {
            std::lock_guard lk(write_mu_);
            // Append framing newline atomically with the payload so a concurrent
            // write can't interleave between body and terminator.
            out_.write(line.data(), static_cast<std::streamsize>(line.size()));
            out_.put('\n');
            out_.flush();
        };
    }

    // Run the read pump on a dedicated thread. The pump terminates on EOF or
    // when stop() is called. On natural EOF (peer closed) the engine's
    // on_transport_closed() fires, failing all in-flight requests with
    // errc::ConnectionLost and invoking its error callback.
    void start(RpcEngine& engine) {
        engine_ = &engine;
        running_.store(true, std::memory_order_release);
        reader_ = std::thread([this, &engine] {
            std::string line;
            while (running_.load(std::memory_order_acquire)) {
                if (!std::getline(in_, line)) break;        // EOF or error
                if (!line.empty()) {
                    if (line.back() == '\r') line.pop_back();
                    try { engine.feed_line(line); }
                    catch (...) { /* never let one frame kill the pump */ }
                }
            }
            const bool was_running = running_.exchange(false, std::memory_order_acq_rel);
            if (was_running) engine.on_transport_closed("eof");
        });
    }

    // Block until the reader thread finishes (i.e. until EOF on the stream).
    void join() {
        if (reader_.joinable() &&
            reader_.get_id() != std::this_thread::get_id())
            reader_.join();
    }

    void stop() {
        running_.store(false, std::memory_order_release);
        // We can't easily interrupt std::getline; users should close the stream
        // (e.g. send EOF) to wake the reader. join() does the rest.
        if (reader_.joinable() &&
            reader_.get_id() != std::this_thread::get_id())
            reader_.join();
    }

    bool running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    std::istream& in_;
    std::ostream& out_;
    std::mutex    write_mu_;
    std::thread   reader_;
    std::atomic<bool> running_{false};
    RpcEngine*    engine_{nullptr};
};

} // namespace acp
