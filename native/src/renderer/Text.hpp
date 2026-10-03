// Text.hpp — fontconfig (match + per-codepoint fallback) → FreeType (full hinting, grayscale,
// whole-pixel advances like GTK/Qt NativeRendering) → HarfBuzz shaping → R8 glyph atlas.
// Glyphs are rasterised at the *buffer* pixel size (logical px × device scale) and pixel-snapped.
#pragma once
#include "renderer/Renderer.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ks::gfx {

struct Face;

struct FontSpec {
    std::string family;
    float px = 11;          // logical pixel size
    bool bold = false;
    bool operator==(const FontSpec&) const = default;
};

class TextLayout {
public:
    // shape `text` for a device scale; cheap to call again with the same inputs (cached)
    void set(const std::string& text, const FontSpec& font, float deviceScale);
    void elide(float maxLogicalWidth);          // right elide with … (0 = off)

    float width() const { return m_width / m_scale; }      // logical
    float height() const { return float(m_ascent + m_descent) / m_scale; }
    float ascent() const { return float(m_ascent) / m_scale; }
    const std::string& text() const { return m_text; }

    // draw with the top-left of the line box at logical (x, y)
    void draw(Renderer& r, float x, float y, Color c) const;

private:
    struct G {
        Face* face;
        uint32_t gid;
        int x;          // pen, buffer px
        int dy;
    };
    void shape();
    std::string m_text;
    FontSpec m_font;
    float m_scale = 0;
    float m_elide = 0;
    std::vector<G> m_glyphs;
    int m_width = 0, m_ascent = 0, m_descent = 0;
};

// measure without keeping a layout
float textWidth(const std::string& text, const FontSpec& font, float deviceScale);

} // namespace ks::gfx
