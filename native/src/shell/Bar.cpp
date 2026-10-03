#include "shell/Bar.hpp"
#include "app/Process.hpp"
#include "config/Config.hpp"
#include "config/Theme.hpp"
#include "shell/Shell.hpp"
#include "system/SysInfo.hpp"
#include "util/Time.hpp"
#include "wayland/Display.hpp"
#include "wayland/Output.hpp"

#include <cmath>

namespace ks::shell {

using anim::Easing;
using gfx::Color;

static cfg::Config& C() { return cfg::config(); }

// ---------------------------------------------------------------- Module

Module::Module() {
    hoverable = true;
    track(m_px);
    onHover = [this](bool h) {
        float base = float(C().num("bar.font_size", 11)) + fontDelta;
        float target = grow && C().flag("bar.hover_grow", true) && h ? float(C().num("bar.font_size", 11)) + 4 : base;
        m_px.to(target, 150, Easing::outCubic());
        relayout();
    };
    onClick = [this](int b) { return clicked ? clicked(b) : false; };
    onScroll = [this](int s) { return scrolled ? scrolled(s) : false; };
}

void Module::setSegments(std::vector<Seg> segs) {
    bool same = segs.size() == m_segs.size();
    for (size_t i = 0; same && i < segs.size(); i++) same = segs[i].text == m_segs[i].text && segs[i].accent == m_segs[i].accent;
    if (same) return;
    m_segs = std::move(segs);
    if (m_labels.size() != m_segs.size()) {
        clear();
        m_labels.clear();
        for (size_t i = 0; i < m_segs.size(); i++) m_labels.push_back(add<ui::Label>());
    }
    restyle();
}

void Module::restyle() {
    auto& t = theme();
    if (!m_px.running()) m_px.set(float(C().num("bar.font_size", 11)) + fontDelta);
    bool accentLabels = C().flag("bar.accent_labels", false);
    for (size_t i = 0; i < m_labels.size(); i++) {
        auto* l = m_labels[i];
        l->text = (i ? " " : "") + m_segs[i].text;
        l->font = {t.font, m_px.get(), bold};
        l->color.set((m_segs[i].accent || accentAll) && accentLabels ? t.accent : t.text);
    }
    relayout();
}

float Module::implicitWidth() {
    if (!shown || m_segs.empty() || (m_segs.size() == 1 && m_segs[0].text.empty())) return 0;
    float tw = 0;
    for (auto* l : m_labels) {
        l->pixelSize.set(std::round(m_px.get()));
        tw += l->implicitWidth();
    }
    return marL + padL + tw + padR + marR;
}

void Module::layout() {
    visible = shown && !m_segs.empty() && !(m_segs.size() == 1 && m_segs[0].text.empty());
    float cx = marL + padL;
    float base = float(C().num("bar.font_size", 11));
    for (auto* l : m_labels) {
        l->pixelSize.set(std::round(m_px.get()));
        float lw = l->implicitWidth(), lh = l->implicitHeight();
        // GTK sits the bigger glyphs 1px lower
        l->setGeometry(cx, std::round((h - lh) / 2) + (fontDelta > 0 && m_px.get() > base ? 1 : 0), lw, lh);
        cx += lw;
    }
}

// ---------------------------------------------------------------- Bar

Bar::Bar(Shell& shell, wl::Output* output) : m_shell(shell), m_output(output) {
    bool bottom = C().str("bar.position", "top") == "bottom";
    int h = C().integer("bar.height", 28);
    wl::LayerSurface::Params p;
    p.output = output;
    p.layer = wl::LayerSurface::Top;
    p.ns = "quickshell-bar";       // the user's compositor layerrules match quickshell.*
    p.anchor = (bottom ? wl::LayerSurface::AnchorBottom : wl::LayerSurface::AnchorTop) | wl::LayerSurface::AnchorLeft | wl::LayerSurface::AnchorRight;
    p.height = uint32_t(h);
    p.exclusiveZone = wm().kind() == Wm::Kind::Mango && !bottom ? h - 5 : h;
    m_surface = std::make_unique<wl::LayerSurface>(wl::Display::get(), p);
    m_root = std::make_unique<ui::Root>();

    auto* surf = m_surface.get();
    auto* root = m_root.get();
    root->requestFrame = [surf] { surf->requestRedraw(); };
    root->setCursor = [](wl::Cursor c) {
        if (auto* s = wl::Display::get().seat()) s->setCursor(c);
    };
    surf->onResize = [this](int w, int hh) {
        m_root->w = float(w);
        m_root->h = float(hh);
        m_root->needLayout();
    };
    surf->onScaleChanged = [this] {
        m_root->deviceScale = float(m_surface->scale());
        reconfigure();
    };
    surf->onTick = [root](double now) { return root->tick(now); };
    surf->onRender = [root](gfx::Renderer& r) { root->render(r); };
    surf->onPointerMotion = [root](double x, double y) { root->pointerMotion(float(x), float(y)); };
    surf->onPointerLeave = [root] { root->pointerLeave(); };
    surf->onPointerButton = [root](uint32_t b, bool pressed) {
        int btn = b == 0x110 ? ui::Left : b == 0x111 ? ui::Right : b == 0x112 ? ui::Middle : 0;
        if (btn) root->pointerButton(btn, pressed);
    };
    surf->onPointerScroll = [root](int s) { root->pointerScroll(s); };
    root->deviceScale = float(surf->scale());

    build();
    reconfigure();
    refreshWorkspaces();
    tickClock();
    refreshStats();
}

Bar::~Bar() {
    m_root.reset();
    m_surface.reset();
}

ui::Rect* Bar::island() {
    auto* r = m_root->add<ui::Rect>();
    return r;
}

void Bar::build() {
    m_bg = m_root->add<ui::Rect>();

    // ---- left: workspaces (+ title)
    m_wsIsland = island();
    m_wsRow = m_wsIsland->add<ui::Row>();
    m_wsRow->centerY = true;
    m_wsPadL = m_wsRow->add<ui::Widget>();
    m_ws = m_wsRow->add<WsIndicator>();
    m_wsPadR = m_wsRow->add<ui::Widget>();
    m_title = m_wsRow->add<Module>();
    m_title->grow = false;
    m_title->padL = 12;
    m_ws->onActivate = [this](const WsEntry& e) { wm().focus(e, m_output->name()); };
    m_wsIsland->onScroll = [](int steps) {   // scrolling anywhere on the island walks workspaces
        wm().scroll(steps);
        return true;
    };

    // ---- centre: clock
    clockIsland = island();
    m_clock = clockIsland->add<Module>();
    m_clock->clicked = [this](int b) {
        if (b != ui::Left) return false;
        m_shell.togglePanel(m_output);
        return true;
    };
    m_clock->scrolled = [this](int s) {
        m_shell.scrollAction(C().str("bar.scroll_clock", "volume"), s);
        return true;
    };

    // ---- right
    m_rightIsland = island();
    m_rightRow = m_rightIsland->add<ui::Row>();
    auto statScroll = [this](int s) {
        m_shell.scrollAction(C().str("bar.scroll_stats", "volume"), s);
        return true;
    };
    m_cpu = m_rightRow->add<Module>();
    m_cpu->scrolled = statScroll;
    m_ram = m_rightRow->add<Module>();
    m_ram->scrolled = statScroll;
    m_shell.addBarModules(*this, *m_rightRow, statScroll);   // volume, network, tray, media … (other modules)
    m_power = m_rightRow->add<Module>();
    m_power->setText("⏻");
    m_power->accentAll = true;
    m_power->fontDelta = 4;
    m_power->grow = false;
    m_power->padR = 6;
    m_power->marR = 10;
    m_power->scrolled = statScroll;
    m_power->clicked = [](int b) {
        if (b != ui::Left) return false;
        spawnDetached({"kusanagi", "msg", "power", "toggle"});
        return true;
    };

    m_root->onLayout = [this](ui::Widget&) { place(); };
}

void Bar::reconfigure() {
    auto& t = theme();
    bool bottom = C().str("bar.position", "top") == "bottom";
    int h = C().integer("bar.height", 28);
    m_surface->setAnchor((bottom ? wl::LayerSurface::AnchorBottom : wl::LayerSurface::AnchorTop) | wl::LayerSurface::AnchorLeft |
                         wl::LayerSurface::AnchorRight);
    m_surface->setSize(0, uint32_t(h));
    m_surface->setExclusiveZone(wm().kind() == Wm::Kind::Mango && !bottom ? h - 5 : h);

    std::string style = C().str("bar.style", "islands");
    float op = float(C().num("bar.opacity", 0.5));
    float radius = float(C().num("bar.radius", 10));
    bool outline = C().flag("bar.outline", false) && style == "islands";
    for (auto* isl : {m_wsIsland, clockIsland, m_rightIsland}) {
        isl->color.to(style == "islands" ? t.bgPanel.alpha(op) : Color{}, 250);
        isl->radius = radius;
        isl->border = outline ? 1 : 0;
        isl->borderColor.set(t.text.alpha(0.12f));
    }
    m_bg->visible = style == "solid" || style == "floating";
    m_bg->color.set(t.bgPanel.alpha(op));

    m_ws->restyle();
    m_title->shown = C().flag("bar.modules.title", false) && C().str("bar.layout", "classic") != "centered";
    m_clock->bold = C().flag("bar.clock_bold", true);
    m_cpu->shown = C().flag("bar.modules.cpu", true);
    m_ram->shown = C().flag("bar.modules.ram", true);
    m_power->shown = C().flag("bar.modules.power", true);
    for (auto* m : {m_title, m_clock, m_cpu, m_ram, m_power}) m->restyle();
    m_shell.restyleBarModules(*this);
    refreshWorkspaces();
    refreshStats();
    m_root->needLayout();
}

void Bar::place() {
    float W = m_root->w, H = float(C().integer("bar.height", 28));
    bool bottom = C().str("bar.position", "top") == "bottom";
    bool centered = C().str("bar.layout", "classic") == "centered";
    std::string style = C().str("bar.style", "islands");
    float ih = H - 6;
    float iy = bottom ? 2 : 4;

    bool floating = style == "floating";
    m_bg->setGeometry(floating ? 6 : 0, floating ? (bottom ? 1 : 3) : 0, W - 2 * (floating ? 6 : 0), floating ? H - 4 : H);
    m_bg->radius = floating ? float(C().num("bar.radius", 10)) : 0;

    // workspaces: dwl tags run flush from the edge
    bool flush = m_ws->style == "dwl" && !centered;
    m_ws->slotHeight = flush ? H : ih;
    m_wsPadL->w = m_wsPadR->w = flush ? 0 : 8;
    m_wsPadL->h = m_wsPadR->h = 1;
    float wsH = flush ? H : ih;
    m_wsRow->h = wsH;
    m_title->h = wsH;
    float wsW = m_wsRow->implicitWidth();
    m_wsRow->setGeometry(0, 0, wsW, wsH);
    m_wsIsland->setGeometry(flush ? 0 : centered ? std::floor((W - wsW) / 2) : 6, flush ? 0 : iy, wsW, wsH);
    if (flush) m_wsIsland->radius = 0;

    m_clock->h = ih;
    float cw = m_clock->implicitWidth();
    m_clock->setGeometry(0, 0, cw, ih);
    clockIsland->setGeometry(centered ? 6 : std::floor((W - cw) / 2), iy, cw, ih);

    for (auto& c : m_rightRow->children()) c->h = ih;
    m_rightRow->fitHeight = true;
    m_rightRow->h = ih;
    float rw = m_rightRow->implicitWidth();
    m_rightRow->setGeometry(0, 0, rw, ih);
    m_rightIsland->setGeometry(W - 6 - rw, iy, rw, ih);
    m_rightIsland->visible = rw > 0;
}

void Bar::refreshWorkspaces() { m_ws->setEntries(wm().workspaces(m_output->name())); }

void Bar::tickClock() { m_clock->setText(formatQt(C().str("bar.clock", "HH:mm"))); }

void Bar::refreshStats() {
    auto& s = sysinfo();
    m_cpu->setSegments({{"CPU", true}, {std::to_string(s.cpu) + "%", false}});
    m_ram->setSegments({{"RAM", true}, {std::to_string(s.ram) + "%", false}});
}

} // namespace ks::shell
