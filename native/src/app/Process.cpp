#include "app/Process.hpp"
#include "app/EventLoop.hpp"
#include "app/Log.hpp"

#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <spawn.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace ks {
namespace {

std::vector<char*> cargv(const std::vector<std::string>& argv) {
    std::vector<char*> v;
    for (auto& a : argv) v.push_back(const_cast<char*>(a.c_str()));
    v.push_back(nullptr);
    return v;
}

int pidfdOpen(pid_t pid) { return int(syscall(SYS_pidfd_open, pid, 0)); }

// posix_spawn with a clean signal mask/dispositions, new session, stdin from /dev/null
pid_t spawnWith(const std::vector<std::string>& argv, int stdoutFd, bool newSession) {
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (stdoutFd >= 0) posix_spawn_file_actions_adddup2(&fa, stdoutFd, 1);
    else posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t none, all;
    sigemptyset(&none);
    sigfillset(&all);
    posix_spawnattr_setsigmask(&at, &none);
    posix_spawnattr_setsigdefault(&at, &all);
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    if (newSession) flags |= POSIX_SPAWN_SETSID;
    posix_spawnattr_setflags(&at, flags);
    auto args = cargv(argv);
    pid_t pid = -1;
    int err = posix_spawnp(&pid, args[0], &fa, &at, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    if (err) {
        log::warn("process", "can't run {}: {}", argv[0], strerror(err));
        return -1;
    }
    return pid;
}

// reap a child without blocking the loop
void reapLater(pid_t pid, std::function<void(int)> done) {
    int pfd = pidfdOpen(pid);
    if (pfd < 0) {
        int st = 0;
        waitpid(pid, &st, 0);
        if (done) done(st);
        return;
    }
    EventLoop::main().watch(pfd, EPOLLIN, [pid, pfd, done](uint32_t) {
        int st = 0;
        waitpid(pid, &st, WNOHANG);
        EventLoop::main().unwatch(pfd);
        close(pfd);
        if (done) done(st);
    });
}

} // namespace

void spawnDetached(const std::vector<std::string>& argv) {
    if (argv.empty()) return;
    pid_t pid = spawnWith(argv, -1, true);
    if (pid > 0) reapLater(pid, nullptr);
}

void spawnShell(const std::string& command) { spawnDetached({"sh", "-c", command}); }

Process::~Process() { stop(); }

bool Process::start(const std::vector<std::string>& argv) {
    stop();
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return false;
    m_pid = spawnWith(argv, fds[1], false);
    close(fds[1]);
    if (m_pid <= 0) {
        close(fds[0]);
        m_pid = -1;
        return false;
    }
    m_out = fds[0];
    fcntl(m_out, F_SETFL, O_NONBLOCK);
    EventLoop::main().watch(m_out, EPOLLIN | EPOLLHUP, [this](uint32_t) { readable(); });
    m_pidfd = pidfdOpen(m_pid);
    if (m_pidfd >= 0) EventLoop::main().watch(m_pidfd, EPOLLIN, [this](uint32_t) { reap(); });
    return true;
}

void Process::readable() {
    char buf[8192];
    for (;;) {
        ssize_t n = read(m_out, buf, sizeof buf);
        if (n > 0) {
            m_buf.append(buf, size_t(n));
            size_t pos;
            while ((pos = m_buf.find('\n')) != std::string::npos) {
                std::string line = m_buf.substr(0, pos);
                m_buf.erase(0, pos + 1);
                if (onLine) onLine(line);
            }
            continue;
        }
        if (n == 0) {   // EOF
            EventLoop::main().unwatch(m_out);
            close(m_out);
            m_out = -1;
        }
        break;
    }
}

void Process::reap() {
    int st = 0;
    if (m_pid > 0) waitpid(m_pid, &st, WNOHANG);
    if (m_out >= 0) readable();
    if (m_pidfd >= 0) {
        EventLoop::main().unwatch(m_pidfd);
        close(m_pidfd);
        m_pidfd = -1;
    }
    if (m_out >= 0) {
        EventLoop::main().unwatch(m_out);
        close(m_out);
        m_out = -1;
    }
    m_pid = -1;
    if (!m_buf.empty() && onLine) onLine(m_buf);
    m_buf.clear();
    if (onExit) onExit(st);
}

void Process::stop() {
    if (m_pid > 0) {
        kill(m_pid, SIGTERM);
        int st;
        waitpid(m_pid, &st, 0);
        m_pid = -1;
    }
    if (m_pidfd >= 0) {
        EventLoop::main().unwatch(m_pidfd);
        close(m_pidfd);
        m_pidfd = -1;
    }
    if (m_out >= 0) {
        EventLoop::main().unwatch(m_out);
        close(m_out);
        m_out = -1;
    }
    m_buf.clear();
}

void run(const std::vector<std::string>& argv, std::function<void(int, std::string)> done) {
    auto p = std::make_shared<Process>();
    auto out = std::make_shared<std::string>();
    p->onLine = [out](std::string_view l) {
        out->append(l);
        out->push_back('\n');
    };
    p->onExit = [p, out, done](int st) mutable {
        auto keep = p;   // keep the Process alive until we're out of its callback
        EventLoop::main().post([keep] {});
        done(st, std::move(*out));
        p.reset();
    };
    if (!p->start(argv)) done(-1, {});
}

} // namespace ks
