#include "ipc/Ipc.hpp"
#include "app/EventLoop.hpp"
#include "app/Log.hpp"
#include "util/Paths.hpp"

#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ks::ipc {

Server& Server::get() {
    static Server s;
    return s;
}

bool Server::listen() {
    paths::mkdirs(paths::runtimeDir());
    auto sock = paths::socketPath();
    // single instance per session: hold a lock for our lifetime
    m_lock = open((sock + ".lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (m_lock < 0 || flock(m_lock, LOCK_EX | LOCK_NB) != 0) {
        log::error("ipc", "another Kusanagi is already running on this display");
        return false;
    }
    unlink(sock.c_str());
    m_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock.c_str(), sizeof addr.sun_path - 1);
    if (bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(m_fd, 16) != 0) {
        log::error("ipc", "can't listen on {}: {}", sock, strerror(errno));
        return false;
    }
    EventLoop::main().watch(m_fd, EPOLLIN, [this](uint32_t) { accept(); });
    return true;
}

void Server::stop() {
    if (m_fd >= 0) {
        EventLoop::main().unwatch(m_fd);
        close(m_fd);
        unlink(paths::socketPath().c_str());
        m_fd = -1;
    }
}

void Server::on(const std::string& target, const std::string& fn, Handler h, const std::string& help) {
    m_targets[target][fn] = {std::move(h), help};
}

std::string Server::show() const {
    std::string out;
    for (auto& [t, fns] : m_targets) {
        out += "target " + t + "\n";
        for (auto& [f, e] : fns)
            if (!f.empty()) out += "  " + f + (e.help.empty() ? "" : "  " + e.help) + "\n";
    }
    return out;
}

json::Value Server::call(const Args& argv) {
    if (argv.empty()) throw std::runtime_error("empty request");
    auto t = m_targets.find(argv[0]);
    if (t == m_targets.end()) throw std::runtime_error("no target '" + argv[0] + "' (see: kusanagi ipc show)");
    std::string fn = argv.size() > 1 ? argv[1] : "";
    if (auto f = t->second.find(fn); f != t->second.end() && !fn.empty())
        return f->second.h(Args(argv.begin() + 2, argv.end()));
    if (auto any = t->second.find(""); any != t->second.end()) return any->second.h(Args(argv.begin() + 1, argv.end()));
    throw std::runtime_error("target '" + argv[0] + "' has no function '" + fn + "'");
}

void Server::accept() {
    for (;;) {
        int c = accept4(m_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (c < 0) return;
        // requests are tiny: read the line with a short timeout, answer, close
        std::string line;
        char buf[4096];
        pollfd p{c, POLLIN, 0};
        while (line.find('\n') == std::string::npos && poll(&p, 1, 500) > 0) {
            ssize_t n = read(c, buf, sizeof buf);
            if (n <= 0) break;
            line.append(buf, size_t(n));
        }
        json::Value reply;
        auto req = json::parse(line.substr(0, line.find('\n')));
        Args args;
        if (req && req->isArray())
            for (auto& a : req->array()) args.push_back(a.isString() ? a.string() : json::dump(a));
        try {
            json::Value result = call(args);
            reply = json::Object{{"ok", true}, {"result", result}};
        } catch (const std::exception& e) {
            reply = json::Object{{"ok", false}, {"error", std::string(e.what())}};
        }
        std::string out = json::dump(reply) + "\n";
        ssize_t w = write(c, out.data(), out.size());
        (void)w;
        close(c);
    }
}

static int connectSocket() {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, paths::socketPath().c_str(), sizeof addr.sun_path - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int request(const Args& argv, bool quiet) {
    int fd = connectSocket();
    if (fd < 0) {
        if (!quiet) fprintf(stderr, "kusanagi isn't running (no %s)\n", paths::socketPath().c_str());
        return 2;
    }
    json::Array a;
    for (auto& s : argv) a.push_back(s);
    std::string out = json::dump(json::Value(a)) + "\n";
    ssize_t w = write(fd, out.data(), out.size());
    (void)w;
    std::string in;
    char buf[65536];
    pollfd p{fd, POLLIN, 0};
    while (poll(&p, 1, 5000) > 0) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) break;
        in.append(buf, size_t(n));
    }
    close(fd);
    auto reply = json::parse(in.substr(0, in.find('\n')));
    if (!reply) {
        if (!quiet) fprintf(stderr, "kusanagi: no reply\n");
        return 2;
    }
    if (!(*reply)["ok"].boolean()) {
        if (!quiet) fprintf(stderr, "kusanagi: %s\n", (*reply)["error"].string().c_str());
        return 1;
    }
    auto& r = (*reply)["result"];
    if (!quiet && !r.isNull()) {
        if (r.isString()) printf("%s%s", r.string().c_str(), r.string().ends_with('\n') ? "" : "\n");
        else printf("%s\n", json::dump(r, 2).c_str());
    }
    return 0;
}

bool ping() { return request({"ping"}, true) == 0; }

} // namespace ks::ipc
