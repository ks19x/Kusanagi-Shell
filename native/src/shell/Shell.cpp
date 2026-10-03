#include "shell/Shell.hpp"
#include "app/EventLoop.hpp"
#include "app/Log.hpp"
#include "app/Process.hpp"
#include "config/Config.hpp"
#include "config/Theme.hpp"
#include "ipc/Ipc.hpp"
#include "system/SysInfo.hpp"
#include "system/Wm.hpp"
#include "wayland/Display.hpp"
#include "wayland/Output.hpp"

#include <algorithm>
#include <stdexcept>

namespace ks::shell {

Shell& Shell::get() {
    static Shell s;
    return s;
}

bool Shell::wantsBar(wl::Output* o) const {
    auto outs = cfg::config().list("bar.outputs");
    if (outs.empty()) return true;
    return std::any_of(outs.begin(), outs.end(), [&](auto& n) { return n == "*" || n == o->name(); });
}

void Shell::addOutput(wl::Output* o) {
    if (!wantsBar(o)) return;
    log::info("shell", "bar on {}", o->name());
    m_bars.push_back(std::make_unique<Bar>(*this, o));
    o->onChanged = [this, o] {
        for (auto& b : m_bars)
            if (b->output() == o) b->reconfigure();
    };
}

void Shell::removeOutput(wl::Output* o) {
    std::erase_if(m_bars, [&](auto& b) { return b->output() == o; });
}

bool Shell::start() {
    auto& d = wl::Display::get();
    d.onOutputAdded = [this](wl::Output* o) { addOutput(o); };
    d.onOutputRemoved = [this](wl::Output* o) { removeOutput(o); };

    anim::setSpeed(cfg::config().num("look.anim_speed", 1), cfg::config().num("look.bounce", 1));
    wm().onChanged([this] {
        for (auto& b : m_bars) b->refreshWorkspaces();
    });
    sysinfo().onChanged([this] {
        for (auto& b : m_bars) b->refreshStats();
    });
    theme().onChanged([this] {
        anim::setSpeed(cfg::config().num("look.anim_speed", 1), cfg::config().num("look.bounce", 1));
        for (auto& b : m_bars) b->reconfigure();
    });
    // the clock: on the minute (or second if the format shows seconds), surviving suspend
    EventLoop::main().onWallClock(1, [this] {
        static std::string last;
        for (auto& b : m_bars) b->tickClock();
    });
    sysinfo().start();
    for (auto& o : d.outputs())
        if (o->ready()) addOutput(o.get());
    registerIpc();
    return true;
}

void Shell::togglePanel(wl::Output*) {
    // control panel lands in M6; until then the v4 panel can be reached via `kusanagi msg`
    log::info("shell", "panel: not in the native shell yet");
}

void Shell::changeVolume(int steps) {
    int step = cfg::config().integer("bar.volume_step", 5) * std::abs(steps);
    spawnDetached({"wpctl", "set-volume", "-l", "1.0", "@DEFAULT_AUDIO_SINK@", std::to_string(step) + "%" + (steps > 0 ? "+" : "-")});
}

void Shell::scrollAction(const std::string& action, int steps) {
    if (action == "volume") changeVolume(steps);
    else if (action == "workspaces") wm().scroll(steps);
}

void Shell::addBarModules(Bar&, ui::Row&, std::function<bool(int)>) {}
void Shell::restyleBarModules(Bar&) {}

void Shell::registerIpc() {
    auto& s = ipc::Server::get();
    s.on("ping", "", [](const ipc::Args&) { return json::Value("pong"); }, "is it running");
    s.on("version", "", [](const ipc::Args&) { return json::Value(std::string(KUSANAGI_VERSION)); });
    s.on("quit", "", [](const ipc::Args&) {
        EventLoop::main().post([] { EventLoop::main().quit(0); });
        return json::Value(nullptr);
    });
    s.on("reload", "", [](const ipc::Args&) {
        cfg::config().load();
        theme().load();
        theme().notify();
        return json::Value(nullptr);
    }, "re-read config.toml + colors.json");
    s.on("ipc", "show", [](const ipc::Args&) { return json::Value(ipc::Server::get().show()); });
    s.on("outputs", "", [this](const ipc::Args&) {
        json::Array a;
        for (auto& o : wl::Display::get().outputs())
            a.push_back(json::Object{{"name", o->name()}, {"width", o->width()}, {"height", o->height()}, {"x", o->x()}, {"y", o->y()},
                                     {"scale", o->scale()}, {"bar", std::any_of(m_bars.begin(), m_bars.end(), [&](auto& b) { return b->output() == o.get(); })}});
        return json::Value(a);
    });
    s.on("workspaces", "", [](const ipc::Args& args) {
        json::Array a;
        std::string out = args.empty() ? "" : args[0];
        if (out.empty() && !wl::Display::get().outputs().empty()) out = wl::Display::get().outputs()[0]->name();
        for (auto& e : wm().workspaces(out)) a.push_back(json::Object{{"n", e.n}, {"active", e.active}, {"occupied", e.occupied}, {"urgent", e.urgent}});
        return json::Value(a);
    }, "[output]");
    s.on("gamemode", "effects", [](const ipc::Args& a) {
        wm().setEffects(a.empty() || a[0] == "on" || a[0] == "true");
        return json::Value(nullptr);
    });
}

} // namespace ks::shell
