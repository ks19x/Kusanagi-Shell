#include "shell/WsIndicator.hpp"
#include "config/Config.hpp"
#include "config/Theme.hpp"

#include <cmath>
#include <sstream>

namespace ks::shell {

using gfx::Color;
using anim::Easing;

class WsSlot : public ui::Widget {
public:
    WsSlot(WsIndicator* ind) : m_ind(ind) {
        hoverable = true;
        track(m_width);
        dot = add<ui::Rect>();
        dot->track(dotW);
        dot->track(dotH);
        dot->track(glowA);
        dotLabel = dot->add<ui::Label>();
        block = add<ui::Rect>();
        mark = block->add<ui::Rect>();
        label = add<ui::Label>();
        underline = add<ui::Rect>();
        underline->track(lineW);
        onHover = [this](bool) { restyle(false); };
        onClick = [this](int b) {
            if (b == ui::Left && m_ind->onActivate) m_ind->onActivate(entry);
            return b == ui::Left;
        };
    }

    WsEntry entry;
    ui::Rect *dot, *block, *mark, *underline;
    ui::Label *dotLabel, *label;
    anim::Value<float> m_width{0}, dotW{10}, dotH{10}, glowA{0}, lineW{0};

    bool dwl() const { return m_ind->style == "dwl"; }
    bool textStyle() const {
        auto& s = m_ind->style;
        return s == "numbers" || s == "roman" || s == "kanji" || s == "custom" || s == "dwl";
    }

    float implicitWidth() override { return m_width.get(); }
    float implicitHeight() override { return m_ind->slotHeight; }

    void restyle(bool instant) {
        auto& t = theme();
        float barFont = float(cfg::config().num("bar.font_size", 11));
        Color ac = m_ind->activeColor();
        bool ts = textStyle(), pill = entry.active && m_ind->style == "pills";
        bool empty = !entry.occupied;
        float d250 = instant ? 0 : 250, d160 = instant ? 0 : 160;

        // ---- dot styles
        dot->visible = !ts;
        dot->radius = 6;
        dotW.to(pill ? 26 : 10, d250, Easing::outCubic());
        dotH.to(pill ? 14 : 10, d250, Easing::outCubic());
        Color dc = entry.urgent ? t.danger : hovered ? t.text.alpha(0.55f) : entry.active ? ac : empty ? Color{} : t.text.alpha(0.85f);
        dot->color.to(dc, d250, Easing::outCubic());
        dot->border = entry.active ? 0 : 1;
        dot->borderColor.set(empty ? t.text.alpha(0.35f) : Color{});
        dot->shadowBlur = entry.urgent ? 8 : 10;
        dot->shadowColor.set(entry.urgent ? t.danger.alpha(0.5f) : ac.alpha(0.55f));
        glowA.to((entry.active && m_ind->glow) || entry.urgent ? 1.f : 0.f, d250, Easing::outCubic());
        dotLabel->visible = pill;
        dotLabel->text = std::to_string(entry.n);
        dotLabel->font = {t.font, 10, true};
        dotLabel->pixelSize.set(10);
        dotLabel->color.set(t.bgPanel);

        // ---- dwl block
        block->visible = dwl();
        block->color.to(entry.urgent ? t.danger : entry.active ? ac : hovered ? t.text.alpha(0.1f) : Color{}, d160);
        mark->visible = !empty;
        mark->color.set(entry.active ? t.bgPanel : t.text);
        mark->opacity.set(entry.active ? 1.f : 0.8f);

        // ---- glyph styles
        label->visible = ts;
        label->text = m_ind->glyph(entry.n);
        float px = dwl() ? barFont : 12;
        label->font = {t.font, px, entry.active && !dwl()};
        label->pixelSize.set(px);
        Color lc = dwl() && (entry.active || entry.urgent) ? t.bgPanel
                 : entry.urgent ? t.danger
                 : entry.active ? ac
                 : hovered ? t.text
                 : empty ? t.text.alpha(0.35f) : t.text.alpha(0.85f);
        label->color.to(lc, d250, Easing::outCubic());
        underline->visible = ts && !dwl();
        underline->radius = 1;
        underline->color.set(ac);
        lineW.to(entry.active ? std::max(8.f, label->implicitWidth()) : 0.f, d250, Easing::outCubic());

        float target = dwl() ? std::max(m_ind->slotHeight, label->implicitWidth() + 14) : (ts ? label->implicitWidth() + 4 : dotW.target()) + 8;
        m_width.to(target, d250, Easing::outCubic());
        update();
    }

    void layout() override {
        h = m_ind->slotHeight;
        w = m_width.get();
        // pills / dots: 4px in, vertically centred, glow follows the dot
        dot->setGeometry(4, std::round((h - dotH.get()) / 2), dotW.get(), dotH.get());
        dot->shadowColor.set(dot->shadowColor.get());
        float ga = glowA.get();
        if (ga < 0.001f) dot->shadowBlur = 0;
        else {
            Color sc = entry.urgent ? theme().danger.alpha(0.5f * ga) : m_ind->activeColor().alpha(0.55f * ga);
            dot->shadowColor.set(sc);
            dot->shadowBlur = entry.urgent ? 8 : 10;
        }
        dotLabel->setGeometry(0, 0, dot->w, dot->h);
        float lw = dotLabel->implicitWidth();
        dotLabel->x = std::round((dot->w - lw) / 2);
        dotLabel->w = lw;

        block->setGeometry(0, 0, w, h);
        mark->setGeometry(3, 3, 4, 4);

        float tw = label->implicitWidth(), th = label->implicitHeight();
        label->setGeometry(dwl() ? std::round((w - tw) / 2) : 6, std::round((h - th) / 2) + (dwl() ? 0 : -1), tw, th);
        float uw = lineW.get();
        underline->setGeometry(std::round(label->x + (tw - uw) / 2), label->y + th + 1, uw, 2);
        underline->visible = textStyle() && !dwl() && uw > 0.5f;
        if (hovered != m_lastHover) m_lastHover = hovered;
    }

private:
    WsIndicator* m_ind;
    bool m_lastHover = false;
};

WsIndicator::WsIndicator() {
    centerY = false;
    restyle();
}

Color WsIndicator::activeColor() const {
    auto c = cfg::config().str("workspaces.active_color", "accent");
    return c == "accent2" ? theme().accent2 : c == "text" ? theme().text : theme().accent;
}

std::string WsIndicator::glyph(int n) const {
    static const char* roman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    static const char* kanji[] = {"一", "二", "三", "四", "五", "六", "七", "八", "九", "十"};
    if (style == "roman" && n >= 1 && n <= 10) return roman[n - 1];
    if (style == "kanji" && n >= 1 && n <= 10) return kanji[n - 1];
    if (style == "custom") {
        std::istringstream ss(cfg::config().str("workspaces.icons", ""));
        std::string icon;
        for (int i = 1; ss >> icon; i++)
            if (i == n) return icon;
    }
    return std::to_string(n);
}

void WsIndicator::restyle() {
    style = cfg::config().str("workspaces.style", "pills");
    glow = cfg::config().flag("workspaces.glow", true);
    for (auto* s : m_slots) s->restyle(true);
    relayout();
}

void WsIndicator::setEntries(const std::vector<WsEntry>& entries) {
    if (entries == m_entries && !m_slots.empty()) return;
    bool rebuild = entries.size() != m_entries.size();
    for (size_t i = 0; !rebuild && i < entries.size(); i++) rebuild = entries[i].n != m_entries[i].n;
    m_entries = entries;
    if (rebuild) {
        clear();
        m_slots.clear();
        for (auto& e : entries) {
            auto* s = add<WsSlot>(this);
            s->entry = e;
            s->restyle(true);
            m_slots.push_back(s);
        }
    } else {
        for (size_t i = 0; i < entries.size(); i++) {
            m_slots[i]->entry = entries[i];
            m_slots[i]->restyle(false);
        }
    }
    relayout();
}

} // namespace ks::shell
