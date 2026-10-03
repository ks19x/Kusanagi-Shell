// Renderer.hpp — everything Kusanagi draws goes through here; nothing else touches GL.
// One batched GLES 3 pipeline: every primitive is a quad drawn by one shader that switches on a
// per-vertex "kind" — rounded rect (SDF fill + inner border + 2-stop gradient), analytic
// Gaussian rounded-box shadow (no blur pass), atlas glyph, image (optionally rounded).
// Coordinates are logical pixels; the device scale and a 2D affine stack map them to the buffer.
// Opacity multiplies down the stack (Qt semantics: per item, not group opacity).
#pragma once
#include <GLES3/gl3.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace ks::gfx {

struct Color {
    float r = 0, g = 0, b = 0, a = 0;
    static Color rgba(float r, float g, float b, float a = 1) { return {r, g, b, a}; }
    static Color hex(std::string_view s, Color fallback);   // #rgb #rrggbb #aarrggbb (Qt order)
    static Color hex(std::string_view s) { return hex(s, Color{0, 0, 0, 0}); }
    Color alpha(float a) const { return {r, g, b, a}; }
    bool operator==(const Color&) const = default;
};
Color lerp(const Color& a, const Color& b, float t);

struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
};

struct Texture {
    GLuint id = 0;
    int w = 0, h = 0;
};

class Renderer {
public:
    static Renderer& get();
    bool init();

    void beginFrame(int bufferW, int bufferH, float deviceScale);
    void endFrame();

    // ---- state stack
    void save();
    void restore();
    void translate(float x, float y);
    void scaleAbout(float s, float cx, float cy);
    void rotateAbout(float degrees, float cx, float cy);
    void multiplyOpacity(float a);
    void pushClip(RectF r);          // axis-aligned, intersects with the current clip
    void popClip();
    float opacity() const { return m_state.back().opacity; }
    float pixelScale() const;        // logical → buffer px under the current transform

    // ---- primitives
    struct Box {
        Color fill;
        float radius = 0;
        float border = 0;
        Color borderColor;
        Color fill2;                 // gradient end (if gradient)
        bool gradient = false;
        bool horizontal = false;     // gradient direction
    };
    void rect(RectF r, const Box& b);
    void rect(RectF r, Color fill, float radius = 0) { rect(r, Box{.fill = fill, .radius = radius}); }
    void shadow(RectF r, float radius, float blur, Color c);
    void image(const Texture& t, RectF dst, float radius = 0, RectF uv = {0, 0, 1, 1});

    // text: glyph quads already positioned in *buffer* pixels (pixel-snapped by the text layout)
    void glyph(GLuint atlas, float bx, float by, float bw, float bh, float u0, float v0, float u1, float v1, Color c);
    void mapToBuffer(float x, float y, float& bx, float& by) const;

private:
    struct Vertex {
        float px, py;          // buffer pixels
        float lx, ly;          // local, buffer-px offset from the shape's centre
        float hx, hy;          // half size (buffer px)
        float radius, border, kind, extra;
        float u, v;
        uint8_t fill[4], fill2[4], border_c[4];
        float grad;
    };
    struct State {
        float a, b, c, d, tx, ty;   // affine: x' = a x + c y + tx, y' = b x + d y + ty
        float opacity;
        int clip;                    // index into m_clips, -1 = none
    };
    struct Draw {
        GLuint atlas, image;
        int clip;
        uint32_t first, count;
    };
    struct Clip { int x, y, w, h; };

    void quad(float x0, float y0, float x1, float y1, const State& s, float kind, float radius, float border, float extra, Color fill,
              Color fill2, Color bord, bool gradient, bool horizontal, float u0 = 0, float v0 = 0, float u1 = 0, float v1 = 0);
    void beginDraw(GLuint atlas, GLuint image);
    void flush();

    GLuint m_program = 0, m_vao = 0, m_vbo = 0, m_ibo = 0;
    GLint m_uViewport = -1;
    GLuint m_white = 0;               // 1×1 texture bound when a slot is unused
    int m_bw = 0, m_bh = 0;
    float m_deviceScale = 1;
    std::vector<State> m_state;
    std::vector<Clip> m_clips;
    std::vector<int> m_clipStack;
    std::vector<Vertex> m_verts;
    std::vector<uint32_t> m_indices;
    std::vector<Draw> m_draws;
    size_t m_vboCap = 0, m_iboCap = 0;
};

} // namespace ks::gfx
