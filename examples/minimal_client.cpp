// SPDX-License-Identifier: Apache-2.0
//
// examples/minimal_client.cpp — a minimal but complete ACP client.
//
//   Spawns a subprocess (passed as argv[1..]) over a pair of pipes, runs the
//   ACP handshake against it, opens a session, sends a prompt, prints the
//   streamed updates, and shuts down. The companion to examples/echo_agent.
//
//   Usage:
//       ./minimal_client ./echo_agent
//
//   On POSIX systems we use pipe(2)+fork(2)+exec; this keeps the example self-
//   contained (no boost::process etc.). Windows users can wire up CreatePipe +
//   CreateProcess in the same shape.
//
#include <acp/acp.hpp>

#if defined(_WIN32)
#  error "This minimal example uses POSIX fork/exec — please port for Windows."
#endif

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using namespace acp;

namespace {

// Spawn `argv[]` (NULL-terminated) with stdin/stdout pipes connected to the
// returned read/write descriptors. The child inherits stderr.
struct Subprocess {
    pid_t pid;
    int   write_fd;   // we write here → child's stdin
    int   read_fd;    // we read here  ← child's stdout
};

Subprocess spawn(char* const argv[]) {
    int to_child[2], from_child[2];
    if (pipe(to_child) || pipe(from_child)) { std::perror("pipe"); std::exit(1); }
    pid_t pid = fork();
    if (pid < 0) { std::perror("fork"); std::exit(1); }
    if (pid == 0) {
        // Child: hook our stdin from to_child[0], stdout to from_child[1].
        dup2(to_child[0],  STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[1]); close(from_child[0]);
        close(to_child[0]); close(from_child[1]);
        execvp(argv[0], argv);
        std::perror("execvp"); std::_Exit(127);
    }
    close(to_child[0]); close(from_child[1]);
    return {pid, to_child[1], from_child[0]};
}

// fdbuf — minimal istream/ostream around POSIX fds.
class fd_streambuf : public std::streambuf {
public:
    explicit fd_streambuf(int fd, bool reading) : fd_(fd), reading_(reading) {
        if (reading_) setg(buf_, buf_ + sizeof(buf_), buf_ + sizeof(buf_));
        else          setp(buf_, buf_ + sizeof(buf_));
    }
    ~fd_streambuf() override { if (!reading_) sync(); }
protected:
    int underflow() override {
        if (!reading_) return traits_type::eof();
        ssize_t n = ::read(fd_, buf_, sizeof(buf_));
        if (n <= 0) return traits_type::eof();
        setg(buf_, buf_, buf_ + n);
        return traits_type::to_int_type(*gptr());
    }
    int overflow(int c) override {
        if (reading_) return traits_type::eof();
        if (sync() != 0) return traits_type::eof();
        if (c != traits_type::eof()) {
            char ch = static_cast<char>(c);
            if (::write(fd_, &ch, 1) != 1) return traits_type::eof();
        }
        return c;
    }
    int sync() override {
        if (reading_) return 0;
        std::ptrdiff_t n = pptr() - pbase();
        if (n > 0 && ::write(fd_, pbase(), static_cast<std::size_t>(n)) != n) return -1;
        pbump(static_cast<int>(-n));
        return 0;
    }
private:
    int fd_; bool reading_;
    char buf_[8192]{};
};

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <agent-program> [agent-args...]\n";
        return 2;
    }

    // argv from argv+1, NULL-terminated for execvp.
    std::vector<char*> child_argv(argv + 1, argv + argc);
    child_argv.push_back(nullptr);

    Subprocess sp = spawn(child_argv.data());

    fd_streambuf in_buf (sp.read_fd,  /*reading=*/true);
    fd_streambuf out_buf(sp.write_fd, /*reading=*/false);
    std::istream agent_out(&in_buf);
    std::ostream agent_in (&out_buf);

    // Inbound handlers: print streamed session updates.
    ClientHandlers h;
    h.on_session_update = [](const SessionUpdateMsg& m) {
        match(m.update,
            [](const SU_AgentMessageChunk& c) {
                match(c.content,
                    [](const TextContent& t) { std::cerr << "[chunk] " << t.text << "\n"; },
                    [](const auto&)          { std::cerr << "[chunk] <non-text>\n"; });
            },
            [](const SU_Plan& p) {
                std::cerr << "[plan]";
                for (const auto& e : p.entries) std::cerr << " | " << e.content;
                std::cerr << "\n";
            },
            [](const auto&) { std::cerr << "[update] <other>\n"; });
    };

    StdioTransport tx(agent_out, agent_in);
    AgentConnection agent(tx.sink(), std::move(h));
    tx.start(agent.engine());

    // -------- Run the protocol -------------------------------------------
    InitializeParams ip;
    ip.clientCapabilities.fs.readTextFile  = true;
    ip.clientCapabilities.fs.writeTextFile = true;
    ip.clientCapabilities.terminal         = true;
    ip.clientInfo = Just<ImplementationInfo>({"acp-cpp-minimal-client",
                                               Nothing,
                                               Just<std::string>(kLibraryVersion)});
    auto ir = agent.initialize(ip).get();
    std::cerr << "initialized with agent: "
              << (ir.agentInfo.has_value() ? ir.agentInfo->name : "<unknown>") << "\n";
    agent.remember_negotiated(ir);

    NewSessionParams np;
    np.cwd = ".";
    auto nr = agent.session_new(np).get();
    std::cerr << "session: " << nr.sessionId.value << "\n";

    PromptParams pp;
    pp.sessionId = nr.sessionId;
    pp.prompt.push_back(TextContent{"hello acp", Nothing, Json::object()});
    auto pr = agent.session_prompt(pp).get();
    std::cerr << "stopReason: " << to_json(pr.stopReason).get<std::string>() << "\n";

    // Give a beat for any straggling notifications to land.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Closing the agent's stdin signals EOF.
    out_buf.pubsync();
    ::close(sp.write_fd);

    int status = 0;
    ::waitpid(sp.pid, &status, 0);
    return 0;
}
