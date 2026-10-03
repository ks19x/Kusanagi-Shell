// kusanagi — the native shell (v5).
//   kusanagi                     start the shell (or say it's running)
//   kusanagi msg <target> <fn> […]   talk to the running shell (same names as v4)
//   kusanagi ping | quit | reload | version | ipc show
//   kusanagi toggle-launcher | toggle-control-center | toggle-notifications | lock
#include "app/EventLoop.hpp"
#include "app/Log.hpp"
#include "config/Config.hpp"
#include "config/Theme.hpp"
#include "ipc/Ipc.hpp"
#include "renderer/Gl.hpp"
#include "renderer/Renderer.hpp"
#include "shell/Shell.hpp"
#include "system/Wm.hpp"
#include "util/Paths.hpp"
#include "wayland/Display.hpp"

#include <csignal>
#include <cstring>
#include <malloc.h>
#include <sys/signalfd.h>
#include <sys/epoll.h>
#include <unistd.h>

using namespace ks;

static int runShell() {
    if (ipc::ping()) {
        printf("kusanagi is already running\n");
        return 0;
    }
    if (getenv("KUSANAGI_DEBUG")) log::setMinLevel(log::Level::Debug);
    paths::mkdirs(paths::stateDir());
    log::setFile(paths::stateDir() + "/log");
    log::info("kusanagi", "v{} starting", KUSANAGI_VERSION);

    EventLoop loop;
    // SIGINT/SIGTERM through the loop so we shut down cleanly
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    signal(SIGPIPE, SIG_IGN);
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    loop.watch(sfd, EPOLLIN, [&](uint32_t) {
        signalfd_siginfo si;
        while (read(sfd, &si, sizeof si) > 0) {}
        loop.quit(0);
    });

    if (!ipc::Server::get().listen()) return 1;
    cfg::config().load();
    cfg::config().watch();
    theme().load();
    theme().watch();

    wl::Display display;
    if (!display.connect()) return 1;
    if (!gfx::Gl::get().init(display.display())) return 1;
    if (!gfx::Renderer::get().init()) return 1;
    wm().start(display);
    display.roundtrip();
    if (!shell::Shell::get().start()) return 1;

    malloc_trim(0);
    int rc = loop.run();
    log::info("kusanagi", "bye");
    ipc::Server::get().stop();
    return rc;
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "run" || args[0] == "start") return runShell();
    const std::string& cmd = args[0];
    if (cmd == "-v" || cmd == "--version" || cmd == "version") {
        printf("kusanagi %s\n", KUSANAGI_VERSION);
        return 0;
    }
    if (cmd == "-h" || cmd == "--help" || cmd == "help") {
        printf("kusanagi [run] | msg <target> <fn> [args…] | ping | quit | reload | ipc show | version\n"
               "         toggle-launcher | toggle-control-center | toggle-notifications | lock\n");
        return 0;
    }
    if (cmd == "msg") return ipc::request(std::vector<std::string>(args.begin() + 1, args.end()));
    if (cmd == "ping") return ipc::request({"ping"});
    if (cmd == "quit" || cmd == "reload") return ipc::request({cmd});
    if (cmd == "ipc") return ipc::request({"ipc", args.size() > 1 ? args[1] : "show"});
    if (cmd == "toggle-launcher") return ipc::request({"launcher", "toggle"});
    if (cmd == "toggle-control-center") return ipc::request({"panel", "toggle"});
    if (cmd == "toggle-notifications") return ipc::request({"notifs", "toggle"});
    if (cmd == "lock") return ipc::request({"lock", "lock"});
    fprintf(stderr, "kusanagi: unknown command '%s' (see --help)\n", cmd.c_str());
    return 1;
}
