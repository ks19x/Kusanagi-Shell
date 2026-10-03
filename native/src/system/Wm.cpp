#include "system/Wm.hpp"
#include "app/EventLoop.hpp"
#include "app/Log.hpp"
#include "app/Process.hpp"
#include "config/Config.hpp"
#include "util/Json.hpp"
#include "util/Paths.hpp"
#include "wayland/Display.hpp"
#include "wayland/Output.hpp"

#include "ext-workspace-v1-client.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ks {

Wm& Wm::get() {
    static Wm w;
    return w;
}

const char* Wm::name() const {
    switch (m_kind) {
    case Kind::Mango: return "MangoWM";
    case Kind::Hyprland: return "Hyprland";
    case Kind::Niri: return "niri";
    default: return "Wayland";
    }
}

void Wm::changed() {
    for (auto& l : m_listeners) l();
}

// ---------------------------------------------------------------- ext-workspace (Mango / other)

namespace {
Wm::ExtWs* findWs(void* h) {
    for (auto& w : wm().extWs)
        if (w.handle == h) return &w;
    return nullptr;
}
Wm::ExtGroup* findGroup(void* h) {
    for (auto& g : wm().extGroups)
        if (g.handle == h) return &g;
    return nullptr;
}

const ext_workspace_handle_v1_listener kWs = {
    .id = [](void*, ext_workspace_handle_v1*, const char*) {},
    .name = [](void*, ext_workspace_handle_v1* h, const char* n) { if (auto* w = findWs(h)) w->name = n; },
    .coordinates = [](void*, ext_workspace_handle_v1*, wl_array*) {},
    .state = [](void*, ext_workspace_handle_v1* h, uint32_t s) { if (auto* w = findWs(h)) w->state = s; },
    .capabilities = [](void*, ext_workspace_handle_v1*, uint32_t) {},
    .removed = [](void*, ext_workspace_handle_v1* h) {
        auto& v = wm().extWs;
        v.erase(std::remove_if(v.begin(), v.end(), [&](auto& w) { return w.handle == h; }), v.end());
        ext_workspace_handle_v1_destroy(h);
    },
};

const ext_workspace_group_handle_v1_listener kGroup = {
    .capabilities = [](void*, ext_workspace_group_handle_v1*, uint32_t) {},
    .output_enter = [](void*, ext_workspace_group_handle_v1* g, wl_output* o) {
        auto* out = static_cast<wl::Output*>(wl_output_get_user_data(o));
        if (auto* gr = findGroup(g); gr && out) gr->outputs.push_back(out->name());
    },
    .output_leave = [](void*, ext_workspace_group_handle_v1* g, wl_output* o) {
        auto* out = static_cast<wl::Output*>(wl_output_get_user_data(o));
        if (auto* gr = findGroup(g); gr && out) std::erase(gr->outputs, out->name());
    },
    .workspace_enter = [](void*, ext_workspace_group_handle_v1* g, ext_workspace_handle_v1* h) { if (auto* w = findWs(h)) w->group = g; },
    .workspace_leave = [](void*, ext_workspace_group_handle_v1*, ext_workspace_handle_v1* h) { if (auto* w = findWs(h)) w->group = nullptr; },
    .removed = [](void*, ext_workspace_group_handle_v1* g) {
        auto& v = wm().extGroups;
        v.erase(std::remove_if(v.begin(), v.end(), [&](auto& x) { return x.handle == g; }), v.end());
        ext_workspace_group_handle_v1_destroy(g);
    },
};

const ext_workspace_manager_v1_listener kManager = {
    .workspace_group = [](void*, ext_workspace_manager_v1*, ext_workspace_group_handle_v1* g) {
        wm().extGroups.push_back({g, {}});
        ext_workspace_group_handle_v1_add_listener(g, &kGroup, nullptr);
    },
    .workspace = [](void*, ext_workspace_manager_v1*, ext_workspace_handle_v1* h) {
        wm().extWs.push_back({h, "", 0, nullptr});
        ext_workspace_handle_v1_add_listener(h, &kWs, nullptr);
    },
    .done = [](void*, ext_workspace_manager_v1*) { wm().changed(); },
    .finished = [](void*, ext_workspace_manager_v1*) {},
};

std::string configHome() { return paths::env("XDG_CONFIG_HOME", paths::home() + "/.config"); }
} // namespace

void Wm::start(wl::Display& d) {
    if (!paths::env("MANGO_INSTANCE_SIGNATURE").empty()) m_kind = Kind::Mango;
    else if (!paths::env("HYPRLAND_INSTANCE_SIGNATURE").empty()) m_kind = Kind::Hyprland;
    else if (!paths::env("NIRI_SOCKET").empty()) m_kind = Kind::Niri;
    log::info("wm", "compositor: {}", name());

    if (m_kind == Kind::Mango || m_kind == Kind::Other) {
        extManager = d.bind(&ext_workspace_manager_v1_interface, 1);
        if (extManager) ext_workspace_manager_v1_add_listener(static_cast<ext_workspace_manager_v1*>(extManager), &kManager, nullptr);
        else log::warn("wm", "no ext-workspace-v1: no workspaces in the bar");
    } else if (m_kind == Kind::Hyprland) {
        m_hyprLua = std::filesystem::exists(configHome() + "/hypr/hyprland.lua");
        startHyprland();
    } else if (m_kind == Kind::Niri) {
        startNiri();
    }
}

// ---------------------------------------------------------------- Hyprland

namespace {
std::string hyprDir() {
    return paths::env("XDG_RUNTIME_DIR", "/tmp") + "/hypr/" + paths::env("HYPRLAND_INSTANCE_SIGNATURE");
}
int hyprConnect(const std::string& sock) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, (hyprDir() + "/" + sock).c_str(), sizeof a.sun_path - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}
std::string hyprQuery(const std::string& q) {
    int fd = hyprConnect(".socket.sock");
    if (fd < 0) return {};
    ssize_t w = write(fd, q.data(), q.size());
    (void)w;
    std::string out;
    char buf[16384];
    pollfd p{fd, POLLIN, 0};
    while (poll(&p, 1, 1000) > 0) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) break;
        out.append(buf, size_t(n));
    }
    close(fd);
    return out;
}
} // namespace

void Wm::startHyprland() {
    int fd = hyprConnect(".socket2.sock");
    if (fd < 0) {
        log::warn("wm", "can't reach Hyprland's event socket; retrying");
        EventLoop::main().after(EventLoop::Ms(2000), [this] { startHyprland(); });
        return;
    }
    EventLoop::main().watch(fd, EPOLLIN, [this, fd](uint32_t ev) {
        char buf[8192];
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0 || (ev & (EPOLLHUP | EPOLLERR))) {
            EventLoop::main().unwatch(fd);
            close(fd);
            EventLoop::main().after(EventLoop::Ms(2000), [this] { startHyprland(); });
            return;
        }
        m_hbuf.append(buf, size_t(n));
        size_t pos;
        bool refresh = false;
        while ((pos = m_hbuf.find('\n')) != std::string::npos) {
            std::string line = m_hbuf.substr(0, pos);
            m_hbuf.erase(0, pos + 1);
            auto ev2 = line.substr(0, line.find(">>"));
            if (ev2 == "focusedmon" || ev2 == "focusedmonv2") m_hfocusedMon = line.substr(line.find(">>") + 2, line.find(',') - line.find(">>") - 2);
            static const char* kWatch[] = {"workspace", "workspacev2", "createworkspace", "createworkspacev2", "destroyworkspace", "destroyworkspacev2",
                                           "openwindow", "closewindow", "movewindow", "movewindowv2", "focusedmon", "focusedmonv2", "urgent",
                                           "fullscreen", "moveworkspace", "moveworkspacev2", "monitoradded", "monitorremoved", "activewindow"};
            for (auto* w : kWatch)
                if (ev2 == w) refresh = true;
        }
        if (refresh && !m_hrefreshQueued) {
            m_hrefreshQueued = true;
            EventLoop::main().after(EventLoop::Ms(16), [this] { hyprRefresh(); });
        }
    });
    hyprRefresh();
}

void Wm::hyprRefresh() {
    m_hrefreshQueued = false;
    m_hws.clear();
    m_hmonActive.clear();
    if (auto ws = json::parse(hyprQuery("j/workspaces")))
        for (auto& w : ws->array())
            m_hws.push_back({int(w["id"].integer()), w["monitor"].string(), int(w["windows"].integer()), w["hasfullscreen"].boolean()});
    if (auto mons = json::parse(hyprQuery("j/monitors")))
        for (auto& m : mons->array()) {
            m_hmonActive.emplace_back(m["name"].string(), int(m["activeWorkspace"]["id"].integer()));
            if (m["focused"].boolean()) m_hfocusedMon = m["name"].string();
        }
    bool fs = false;
    for (auto& [mon, id] : m_hmonActive)
        if (mon == m_hfocusedMon)
            for (auto& w : m_hws)
                if (w.id == id) fs = w.fullscreen;
    m_fullscreen = fs;
    changed();
}

void Wm::hyprDispatch(const std::string& lua, const std::string& classic) {
    spawnDetached({"hyprctl", "dispatch", m_hyprLua ? lua : classic});
}

// ---------------------------------------------------------------- niri

void Wm::startNiri() {
    auto* p = new Process;
    m_niriProc = p;
    p->onLine = [this](std::string_view l) { niriEvent(l); };
    p->onExit = [this](int) {
        log::warn("wm", "niri event-stream ended; retrying");
        EventLoop::main().after(EventLoop::Ms(2000), [this] {
            delete static_cast<Process*>(m_niriProc);
            startNiri();
        });
    };
    p->start({"niri", "msg", "-j", "event-stream"});
}

void Wm::niriEvent(std::string_view line) {
    auto v = json::parse(line);
    if (!v) return;
    auto& e = *v;
    bool dirty = false;
    if (auto& w = e["WorkspacesChanged"]; !w.isNull()) {
        m_nws.clear();
        for (auto& x : w["workspaces"].array())
            m_nws.push_back({uint64_t(x["id"].integer()), int(x["idx"].integer()), x["output"].string(), x["is_active"].boolean(),
                             x["is_focused"].boolean(), x["is_urgent"].boolean()});
        dirty = true;
    } else if (auto& a = e["WorkspaceActivated"]; !a.isNull()) {
        uint64_t id = uint64_t(a["id"].integer());
        bool focused = a["focused"].boolean();
        std::string out;
        for (auto& w : m_nws)
            if (w.id == id) out = w.output;
        for (auto& w : m_nws) {
            if (w.output == out) w.active = w.id == id;
            if (focused) w.focused = w.id == id;
        }
        dirty = true;
    } else if (auto& u = e["WorkspaceUrgencyChanged"]; !u.isNull()) {
        for (auto& w : m_nws)
            if (w.id == uint64_t(u["id"].integer())) w.urgent = u["urgent"].boolean();
        dirty = true;
    } else if (auto& wc = e["WindowsChanged"]; !wc.isNull()) {
        m_nwin.clear();
        m_nwinFull.clear();
        for (auto& x : wc["windows"].array()) m_nwin.emplace_back(uint64_t(x["id"].integer()), uint64_t(x["workspace_id"].integer()));
        dirty = true;
    } else if (auto& wo = e["WindowOpenedOrChanged"]; !wo.isNull()) {
        auto& x = wo["window"];
        uint64_t id = uint64_t(x["id"].integer());
        std::erase_if(m_nwin, [&](auto& p) { return p.first == id; });
        m_nwin.emplace_back(id, uint64_t(x["workspace_id"].integer()));
        dirty = true;
    } else if (auto& cl = e["WindowClosed"]; !cl.isNull()) {
        uint64_t id = uint64_t(cl["id"].integer());
        std::erase_if(m_nwin, [&](auto& p) { return p.first == id; });
        dirty = true;
    }
    if (dirty) changed();
}

// ---------------------------------------------------------------- queries

std::vector<WsEntry> Wm::workspaces(const std::string& output) const {
    int shown = cfg::config().integer("workspaces.shown", 5);
    std::vector<WsEntry> list;
    if (m_kind == Kind::Mango || m_kind == Kind::Other) {
        // the group on this output (Mango: one per monitor); no group info → all
        for (auto& w : extWs) {
            if (!extGroups.empty() && w.group) {
                bool mine = false;
                for (auto& g : extGroups)
                    if (g.handle == w.group)
                        mine = g.outputs.empty() || std::find(g.outputs.begin(), g.outputs.end(), output) != g.outputs.end();
                if (!mine) continue;
            }
            int n = std::atoi(w.name.c_str());
            if (n < 1 || n > 9) continue;
            bool active = w.state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE;
            bool hidden = w.state & EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN;
            bool urgent = w.state & EXT_WORKSPACE_HANDLE_V1_STATE_URGENT;
            if (n > shown && hidden && !active) continue;
            if (std::any_of(list.begin(), list.end(), [&](auto& e) { return e.n == n; })) continue;
            list.push_back({n, active, !hidden, urgent, w.handle});
        }
    } else if (m_kind == Kind::Hyprland) {
        int act = -1;
        std::string mon = output;
        for (auto& [m, id] : m_hmonActive)
            if (m == mon) act = id;
        std::vector<int> ids;
        for (int i = 1; i <= shown; i++) ids.push_back(i);
        for (auto& w : m_hws)
            if (w.id > 0 && w.monitor == mon && std::find(ids.begin(), ids.end(), w.id) == ids.end()) ids.push_back(w.id);
        for (int id : ids) {
            const HyprWs* w = nullptr;
            for (auto& x : m_hws)
                if (x.id == id) w = &x;
            list.push_back({id, id == act, w && w->windows > 0, false, nullptr});
        }
    } else if (m_kind == Kind::Niri) {
        std::vector<const NiriWs*> mine;
        for (auto& w : m_nws)
            if (w.output == output) mine.push_back(&w);
        std::sort(mine.begin(), mine.end(), [](auto* a, auto* b) { return a->idx < b->idx; });
        for (auto* w : mine) {
            bool occ = std::any_of(m_nwin.begin(), m_nwin.end(), [&](auto& p) { return p.second == w->id; });
            if (w->idx > shown && !occ && !w->active) continue;
            list.push_back({w->idx, w->active, occ, w->urgent, nullptr});
        }
        for (int n = int(list.size()) + 1; n <= shown; n++) list.push_back({n, false, false, false, nullptr});
    }
    std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.n < b.n; });
    return list;
}

int Wm::activeIndex(const std::string& output) const {
    for (auto& e : workspaces(output))
        if (e.active) return e.n;
    return 1;
}

void Wm::focus(const WsEntry& e, const std::string& output) {
    switch (m_kind) {
    case Kind::Mango:
    case Kind::Other:
        if (e.handle && extManager) {
            ext_workspace_handle_v1_activate(static_cast<ext_workspace_handle_v1*>(e.handle));
            ext_workspace_manager_v1_commit(static_cast<ext_workspace_manager_v1*>(extManager));
        }
        break;
    case Kind::Hyprland:
        hyprDispatch(std::format("hl.dsp.focus({{workspace=\"{}\"}})", e.n), std::format("workspace {}", e.n));
        break;
    case Kind::Niri: spawnDetached({"niri", "msg", "action", "focus-workspace", std::to_string(e.n)}); break;
    }
}

void Wm::scroll(int steps) {
    bool prev = steps > 0;
    switch (m_kind) {
    case Kind::Mango: spawnDetached({"mmsg", "dispatch", prev ? "viewtoleft,0" : "viewtoright,0"}); break;
    case Kind::Hyprland:
        hyprDispatch(std::format("hl.dsp.focus({{workspace=\"e{}\"}})", prev ? "-1" : "+1"), std::format("workspace e{}", prev ? "-1" : "+1"));
        break;
    case Kind::Niri: spawnDetached({"niri", "msg", "action", prev ? "focus-workspace-up" : "focus-workspace-down"}); break;
    default: break;
    }
}

void Wm::setEffects(bool on) {
    if (m_kind == Kind::Mango) {
        // the user's configured values, so turning effects back on restores them exactly
        std::map<std::string, int> d{{"blur", 1}, {"shadows", 1}, {"animations", 1}, {"layer_animations", 1}};
        for (auto* f : {"/mango/config.conf", "/mango/rice.conf"})
            if (auto txt = paths::readFile(configHome() + f)) {
                size_t pos = 0;
                while (pos < txt->size()) {
                    size_t end = txt->find('\n', pos);
                    std::string l = txt->substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                    for (auto& [k, v] : d)
                        if (l.starts_with(k + "=")) v = std::atoi(l.c_str() + k.size() + 1);
                    if (end == std::string::npos) break;
                    pos = end + 1;
                }
            }
        std::string cmd;
        for (auto& [k, v] : d) cmd += std::format("mmsg dispatch setoption,{},{}; ", k, on ? v : 0);
        spawnShell(cmd);
    } else if (m_kind == Kind::Hyprland) {
        const char* b = on ? "true" : "false";
        if (m_hyprLua)
            spawnDetached({"hyprctl", "eval",
                           std::format("hl.config({{ decoration = {{ blur = {{ enabled = {} }}, shadow = {{ enabled = {} }} }}, animations = {{ enabled = {} }} }})", b, b, b)});
        else
            spawnDetached({"hyprctl", "--batch",
                           std::format("keyword decoration:blur:enabled {0}; keyword decoration:shadow:enabled {0}; keyword animations:enabled {0}", on ? 1 : 0)});
    }
}

void Wm::quit() {
    switch (m_kind) {
    case Kind::Mango: spawnDetached({"mmsg", "dispatch", "quit"}); break;
    case Kind::Hyprland: hyprDispatch("hl.dsp.exit()", "exit"); break;
    case Kind::Niri: spawnDetached({"niri", "msg", "action", "quit", "--skip-confirmation"}); break;
    default: spawnShell("loginctl terminate-session \"$XDG_SESSION_ID\""); break;
    }
}

std::vector<std::string> Wm::monitorsCommand() const {
    switch (m_kind) {
    case Kind::Mango: return {"mmsg", "get", "all-monitors"};
    case Kind::Hyprland: return {"hyprctl", "monitors", "-j"};
    case Kind::Niri: return {"niri", "msg", "-j", "outputs"};
    default: return {};
    }
}

} // namespace ks
