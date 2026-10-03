// Wm.hpp — everything Kusanagi needs from the compositor, per output:
//   MangoWM / other: ext-workspace-v1 (groups ↔ outputs; hidden = no windows)
//   Hyprland:        .socket2 event stream + .socket queries (j/workspaces, j/monitors)
//   niri:            `niri msg -j event-stream`
// Workspaces come back 1-based and padded to workspaces.shown, like v4's Wm.workspaces.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ks::wl { class Display; }

namespace ks {

struct WsEntry {
    int n = 0;
    bool active = false, occupied = false, urgent = false;
    void* handle = nullptr;      // ext-workspace handle (Mango/other)
    bool operator==(const WsEntry& o) const { return n == o.n && active == o.active && occupied == o.occupied && urgent == o.urgent; }
};

class Wm {
public:
    enum class Kind { Mango, Hyprland, Niri, Other };
    static Wm& get();
    void start(wl::Display& d);

    Kind kind() const { return m_kind; }
    const char* name() const;
    bool hyprLua() const { return m_hyprLua; }

    std::vector<WsEntry> workspaces(const std::string& output) const;
    int activeIndex(const std::string& output) const;
    void focus(const WsEntry& e, const std::string& output);
    void scroll(int steps);
    bool fullscreen() const { return m_fullscreen; }
    void setEffects(bool on);
    void quit();
    std::vector<std::string> monitorsCommand() const;

    void onChanged(std::function<void()> fn) { m_listeners.push_back(std::move(fn)); }
    void changed();

    // backend state (public for the protocol listeners)
    struct ExtWs { void* handle; std::string name; uint32_t state = 0; void* group = nullptr; };
    struct ExtGroup { void* handle; std::vector<std::string> outputs; };
    std::vector<ExtWs> extWs;
    std::vector<ExtGroup> extGroups;
    void* extManager = nullptr;
    bool m_fullscreen = false;

private:
    void startHyprland();
    void hyprRefresh();
    void hyprDispatch(const std::string& lua, const std::string& classic);
    void startNiri();
    void niriEvent(std::string_view line);

    Kind m_kind = Kind::Other;
    bool m_hyprLua = false;
    std::vector<std::function<void()>> m_listeners;
    // hyprland
    struct HyprWs { int id; std::string monitor; int windows; bool fullscreen; };
    std::vector<HyprWs> m_hws;
    std::vector<std::pair<std::string, int>> m_hmonActive;   // monitor → active ws
    std::string m_hfocusedMon;
    bool m_hrefreshQueued = false;
    std::string m_hbuf;
    // niri
    struct NiriWs { uint64_t id; int idx; std::string output; bool active, focused, urgent; };
    std::vector<NiriWs> m_nws;
    std::vector<std::pair<uint64_t, uint64_t>> m_nwin;        // window id → workspace id
    std::vector<std::pair<uint64_t, bool>> m_nwinFull;
    void* m_niriProc = nullptr;
};

inline Wm& wm() { return Wm::get(); }

} // namespace ks
