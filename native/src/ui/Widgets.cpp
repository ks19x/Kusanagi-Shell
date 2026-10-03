#include "ui/Widgets.hpp"

#include <cmath>

namespace ks::ui {

Rect::Rect() {
    track(color);
    track(borderColor);
    track(shadowColor);
}

void Rect::paintSelf(gfx::Renderer& r) {
    if (shadowBlur > 0 && shadowColor.get().a > 0.001f) r.shadow({0, shadowOffsetY, w, h}, radius, shadowBlur, shadowColor.get());
    r.rect({0, 0, w, h}, gfx::Renderer::Box{
                             .fill = color.get(),
                             .radius = radius,
                             .border = border,
                             .borderColor = borderColor.get(),
                             .fill2 = color2,
                             .gradient = gradient,
                             .horizontal = horizontal,
                         });
}

Label::Label() {
    track(pixelSize);
    track(color);
}

void Label::prepare() {
    gfx::FontSpec f = font;
    f.px = std::round(pixelSize.get());
    float scale = root() ? root()->deviceScale : 1.f;
    m_layout.set(text, f, scale);
    if (maxWidth > 0) m_layout.elide(maxWidth);
}

float Label::implicitWidth() {
    prepare();
    return m_layout.width();
}

float Label::implicitHeight() {
    prepare();
    return m_layout.height();
}

void Label::paintSelf(gfx::Renderer& r) {
    prepare();
    m_layout.draw(r, 0, std::round((h - m_layout.height()) / 2), color.get());
}

float Row::implicitWidth() {
    float total = 0;
    int n = 0;
    for (auto& c : children()) {
        if (!c->visible) continue;
        total += c->implicitWidth();
        n++;
    }
    return total + spacing * float(std::max(0, n - 1));
}

void Row::layout() {
    float cx = 0;
    for (auto& c : children()) {
        if (!c->visible) continue;
        float cw = c->implicitWidth();
        float ch = fitHeight ? h : c->implicitHeight();
        c->setGeometry(cx, centerY ? std::round((h - ch) / 2) : c->y, cw, ch);
        cx += cw + spacing;
    }
}

float Column::implicitHeight() {
    float total = 0;
    int n = 0;
    for (auto& c : children()) {
        if (!c->visible) continue;
        total += c->implicitHeight();
        n++;
    }
    return total + spacing * float(std::max(0, n - 1));
}

float Column::implicitWidth() {
    float m = 0;
    for (auto& c : children())
        if (c->visible) m = std::max(m, c->implicitWidth());
    return m;
}

void Column::layout() {
    float cy = 0;
    for (auto& c : children()) {
        if (!c->visible) continue;
        float ch = c->implicitHeight();
        c->setGeometry(c->x, cy, c->implicitWidth(), ch);
        cy += ch + spacing;
    }
}

std::string utf8(uint32_t cp) {
    std::string out;
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    return out;
}

} // namespace ks::ui
