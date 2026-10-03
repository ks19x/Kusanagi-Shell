// Widgets.hpp — the basic pieces: Rect (box + optional shadow), Label (text), Row / Column
// (box layouts), Icon (Nerd glyph by codepoint), Image (texture).
#pragma once
#include "renderer/Text.hpp"
#include "ui/Widget.hpp"

#include <string>

namespace ks::ui {

class Rect : public Widget {
public:
    Rect();
    anim::Value<gfx::Color> color;
    anim::Value<gfx::Color> borderColor;
    float radius = 0;
    float border = 0;
    bool gradient = false, horizontal = false;
    gfx::Color color2;
    // shadow (RectangularShadow): blur 0 = none
    float shadowBlur = 0, shadowOffsetY = 0;
    anim::Value<gfx::Color> shadowColor;
    void paintSelf(gfx::Renderer& r) override;
};

class Label : public Widget {
public:
    Label();
    std::string text;
    gfx::FontSpec font{"JetBrainsMono Nerd Font", 11, false};
    anim::Value<float> pixelSize{11};      // animatable font size (snapped to whole px like Qt)
    anim::Value<gfx::Color> color;
    float maxWidth = 0;                    // right elide
    float implicitWidth() override;
    float implicitHeight() override;
    void paintSelf(gfx::Renderer& r) override;
    void setText(const std::string& t) { if (t != text) { text = t; relayout(); } }

private:
    void prepare();
    gfx::TextLayout m_layout;
};

class Row : public Widget {
public:
    float spacing = 0;
    bool centerY = true;                   // vertically centre children in the row height
    bool fitHeight = false;                // children take the row's height
    float implicitWidth() override;
    void layout() override;
};

class Column : public Widget {
public:
    float spacing = 0;
    float implicitHeight() override;
    float implicitWidth() override;
    void layout() override;
};

// a Nerd Font / symbol glyph by codepoint
std::string utf8(uint32_t cp);

} // namespace ks::ui
