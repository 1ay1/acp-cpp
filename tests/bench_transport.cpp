// SPDX-License-Identifier: Apache-2.0
//
// bench_transport.cpp — throughput benchmark for the FdTransport hot path.
//
//   Measures how many session/update-sized notifications per second the
//   transport + engine can push through a real OS pipe, comparing:
//
//     • unbatched : one writev(2) + implicit flush per frame
//     • batched   : the whole burst coalesced into one writev(2)
//
//   This is the number behind the "fastest ACP server" claim. It is a
//   benchmark, not a pass/fail test (though it asserts a floor so a future
//   regression that halves throughput trips CI).
//
#include <acp/rpc.hpp>
#include <acp/stdio.hpp>
#include <acp/methods.hpp>
#include <acp/updates.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

#if !defined(_WIN32)
#  include <unistd.h>
#endif

using namespace acp;
using clock_t_ = std::chrono::steady_clock;

namespace {

// A representative session/update agent-message-chunk notification payload.
Json make_update(int i) {
    SessionUpdateMsg msg;
    msg.sessionId = SessionId{std::string("sess_bench")};
    msg.update = SU_AgentMessageChunk{
        TextContent{"token-" + std::to_string(i) + " streamed content chunk",
                    Nothing, Json::object()},
        Nothing};
    return to_json(msg);
}

} // namespace

int main() {
#if defined(_WIN32)
    std::cout << "bench_transport: skipped on Windows\n";
    return 0;
#else
    constexpr int kFrames = 100'000;

    // A pipe: writer end = out_fd of the sender transport; reader end drained
    // by a thread that just counts newline-delimited frames.
    int fds[2];
    if (::pipe(fds) != 0) { std::perror("pipe"); return 1; }
    const int rd = fds[0], wr = fds[1];

    std::atomic<long> frames_read{0};
    std::atomic<bool> reader_go{true};
    std::thread reader([&] {
        char buf[64 * 1024];
        while (reader_go.load(std::memory_order_acquire)) {
            ssize_t n = ::read(rd, buf, sizeof buf);
            if (n <= 0) break;
            for (ssize_t k = 0; k < n; ++k)
                if (buf[k] == '\n') frames_read.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Sender: an engine writing through an FdTransport bound to the pipe's
    // write end. We only exercise the write path (no inbound), so we give the
    // transport an invalid read fd it never uses here.
    FdTransport tx(/*in_fd=*/-1, /*out_fd=*/wr);
    RpcEngine   eng(tx.sink());

    auto run = [&](bool batched) {
        frames_read.store(0);
        const auto t0 = clock_t_::now();
        if (batched) {
            auto b = tx.batch();
            for (int i = 0; i < kFrames; ++i)
                eng.notify(method::SessionUpdate, SessionUpdateMsg{
                    SessionId{std::string("sess_bench")},
                    SU_AgentMessageChunk{TextContent{"token-" + std::to_string(i),
                                                     Nothing, Json::object()}, Nothing},
                    Json::object()});
        } else {
            for (int i = 0; i < kFrames; ++i)
                eng.notify(method::SessionUpdate, SessionUpdateMsg{
                    SessionId{std::string("sess_bench")},
                    SU_AgentMessageChunk{TextContent{"token-" + std::to_string(i),
                                                     Nothing, Json::object()}, Nothing},
                    Json::object()});
        }
        const auto t1 = clock_t_::now();
        const double secs =
            std::chrono::duration<double>(t1 - t0).count();
        const double fps = kFrames / secs;
        std::printf("  %-10s %8d frames in %7.3f ms  =>  %10.0f frames/s\n",
                    batched ? "batched" : "unbatched",
                    kFrames, secs * 1e3, fps);
        return fps;
    };

    std::cout << "bench_transport (" << kFrames << " session/update frames over a pipe):\n";
    double fps_unbatched = run(false);
    double fps_batched   = run(true);

    // Let the reader drain, then stop it.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    reader_go.store(false, std::memory_order_release);
    ::close(wr);
    reader.join();
    ::close(rd);

    // Regression floor: anything below this means the hot path has rotted.
    // (Real hardware is far above this; the floor just guards against a 5-10x
    // regression such as re-introducing per-frame flush through std::ostream.)
    assert(fps_unbatched > 50'000.0 && "unbatched throughput regressed");
    assert(fps_batched   > 50'000.0 && "batched throughput regressed");

    std::cout << "bench_transport OK\n";
    return 0;
#endif
}
