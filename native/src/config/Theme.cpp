#include "config/Theme.hpp"
#include "app/EventLoop.hpp"
#include "config/Config.hpp"
#include "util/Json.hpp"
#include "util/Paths.hpp"

#include <map>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <unistd.h>

namespace ks {

namespace {
struct Palette { const char *bg, *card, *text, *dim, *faint, *accent, *accent2, *danger, *ok; };
const std::map<std::string, Palette> kPalettes = {
    {"catppuccin-mocha", {"#1e1e2e", "#313244", "#cdd6f4", "#a6adc8", "#585b70", "#cba6f7", "#f5c2e7", "#f38ba8", "#a6e3a1"}},
    {"catppuccin-latte", {"#eff1f5", "#ccd0da", "#4c4f69", "#6c6f85", "#9ca0b0", "#8839ef", "#ea76cb", "#d20f39", "#40a02b"}},
    {"gruvbox", {"#1d2021", "#32302f", "#ebdbb2", "#a89984", "#504945", "#fabd2f", "#fe8019", "#fb4934", "#b8bb26"}},
    {"nord", {"#2e3440", "#3b4252", "#eceff4", "#d8dee9", "#4c566a", "#88c0d0", "#81a1c1", "#bf616a", "#a3be8c"}},
    {"rose-pine", {"#191724", "#26233a", "#e0def4", "#908caa", "#403d52", "#ebbcba", "#c4a7e7", "#eb6f92", "#9ccfd8"}},
    {"tokyo-night", {"#1a1b26", "#24283b", "#c0caf5", "#a9b1d6", "#414868", "#7aa2f7", "#bb9af7", "#f7768e", "#9ece6a"}},
    {"everforest", {"#272e33", "#374145", "#d3c6aa", "#9da9a0", "#4f5b58", "#a7c080", "#83c092", "#e67e80", "#a7c080"}},
    {"kanagawa", {"#1f1f28", "#2a2a37", "#dcd7ba", "#c8c093", "#54546d", "#7e9cd8", "#957fb8", "#e82424", "#98bb6c"}},
    {"mono", {"#0e0e0e", "#1c1c1c", "#e6e6e6", "#9a9a9a", "#3a3a3a", "#e6e6e6", "#b0b0b0", "#ff5f5f", "#8fd18f"}},
};
} // namespace

Theme& Theme::get() {
    static Theme t;
    return t;
}

gfx::Color Theme::surfaceBorder() const {
    return cfg::config().flag("look.border_accent", false) ? accent.alpha(0.55f) : text.alpha(0.08f);
}
float Theme::surfaceBorderWidth() const { return cfg::config().flag("look.borders", true) ? 1.f : 0.f; }

void Theme::load() {
    using gfx::Color;
    auto& c = cfg::config();
    font = c.str("look.font", "JetBrainsMono Nerd Font");
    json::Value j;
    if (auto txt = paths::readFile(paths::configDir() + "/colors.json"))
        if (auto v = json::parse(*txt)) j = *v;
    auto jc = [&](const char* k, const char* def) { return Color::hex(j[k].isString() ? j[k].string() : def, Color::hex(def)); };
    text = jc("text", "#ffffff");
    textDim = jc("textDim", "#c2c2c2");
    danger = jc("danger", "#ff003c");
    accent = jc("accent", "#ffffff");
    accent2 = jc("accent2", "#ffffff");
    border = jc("border", "#151515");
    bgPanel = jc("bgPanel", "#050505");
    bgCard = jc("bgCard", "#0d0d0d");
    borderAccent = jc("borderAccent", "#2a2a2a");
    textFaint = jc("textFaint", "#4a4a4a");
    ok = jc("ok", "#00ff9c");
    trackBg = jc("trackBg", "#161616");
    if (auto it = kPalettes.find(c.str("look.palette", "wallpaper")); it != kPalettes.end()) {
        auto& p = it->second;
        text = Color::hex(p.text); textDim = Color::hex(p.dim); danger = Color::hex(p.danger);
        accent = Color::hex(p.accent); accent2 = Color::hex(p.accent2); border = Color::hex(p.card);
        bgPanel = Color::hex(p.bg); bgCard = Color::hex(p.card); borderAccent = Color::hex(p.faint);
        textFaint = Color::hex(p.faint); ok = Color::hex(p.ok); trackBg = Color::hex(p.card);
    }
    if (auto pin = c.str("look.accent", ""); !pin.empty()) accent = Color::hex(pin, accent);
}

void Theme::notify() {
    for (auto& l : m_listeners) l();
}

void Theme::watch() {
    m_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    inotify_add_watch(m_inotify, paths::configDir().c_str(), IN_CLOSE_WRITE | IN_MOVED_TO);
    EventLoop::main().watch(m_inotify, EPOLLIN, [this](uint32_t) {
        char buf[4096];
        bool changed = false;
        ssize_t n;
        while ((n = read(m_inotify, buf, sizeof buf)) > 0)
            for (char* p = buf; p < buf + n;) {
                auto* ev = reinterpret_cast<inotify_event*>(p);
                if (ev->len && std::string(ev->name) == "colors.json") changed = true;
                p += sizeof(inotify_event) + ev->len;
            }
        if (changed) {
            load();
            notify();
        }
    });
    cfg::config().onChanged([this] {
        load();
        notify();
    });
}

} // namespace ks
