// Bar.hpp — one bar per output (v4 Bar.qml): left workspaces island (+ window title), centre clock
// island, right island of modules. Styles islands | solid | floating | clear, top/bottom,
// classic/centered layout, waybar metrics (22px islands at height 28, 11px text, hover-grow).
#pragma once
#include "shell/WsIndicator.hpp"
#include "ui/Widgets.hpp"
#include "wayland/LayerSurface.hpp"

#include <memory>

namespace ks::shell {

class Shell;

// one waybar-style module: margin 0 2px, padding 0 10px, text grows +4px on hover
class Module : public ui::Widget {
public:
    Module();
    struct Seg { std::string text; bool accent = false; };
    void setSegments(std::vector<Seg> segs);
    void setText(const std::string& t) { setSegments({{t, false}}); }
    bool shown = true;
    bool bold = false;
    bool grow = true;
    bool accentAll = false;        // icon-only module tinted with bar.accent_labels
    float fontDelta = 0;           // + bar.font_size
    float padL = 10, padR = 10, marL = 2, marR = 2;
    std::string tooltip;
    std::function<bool(int)> clicked;
    std::function<bool(int)> scrolled;
    void restyle();
    float implicitWidth() override;
    void layout() override;

private:
    std::vector<Seg> m_segs;
    std::vector<ui::Label*> m_labels;
    anim::Value<float> m_px{11};
};

class Bar {
public:
    Bar(Shell& shell, wl::Output* output);
    ~Bar();
    wl::Output* output() const { return m_output; }
    void reconfigure();            // config / theme changed
    void refreshWorkspaces();
    void tickClock();
    void refreshStats();

    ui::Rect* clockIsland = nullptr;

private:
    void build();
    void place();
    ui::Rect* island();

    Shell& m_shell;
    wl::Output* m_output;
    std::unique_ptr<wl::LayerSurface> m_surface;
    std::unique_ptr<ui::Root> m_root;
    ui::Rect* m_bg = nullptr;
    ui::Rect* m_wsIsland = nullptr;
    ui::Rect* m_rightIsland = nullptr;
    ui::Row* m_wsRow = nullptr;
    ui::Row* m_rightRow = nullptr;
    WsIndicator* m_ws = nullptr;
    Module* m_title = nullptr;
    Module* m_clock = nullptr;
    Module *m_cpu = nullptr, *m_ram = nullptr, *m_power = nullptr;
    ui::Widget *m_wsPadL = nullptr, *m_wsPadR = nullptr;
};

} // namespace ks::shell
