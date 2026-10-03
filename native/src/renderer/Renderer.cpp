#include "renderer/Renderer.hpp"
#include "app/Log.hpp"

#include <algorithm>
#include <cmath>

namespace ks::gfx {

namespace {

const char* kVertex = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aLocal;
layout(location = 2) in vec2 aHalf;
layout(location = 3) in vec4 aParams;
layout(location = 4) in vec2 aUv;
layout(location = 5) in vec4 aFill;
layout(location = 6) in vec4 aFill2;
layout(location = 7) in vec4 aBorder;
layout(location = 8) in float aGrad;
uniform vec2 uViewport;
out vec2 vLocal;
out vec2 vHalf;
out vec4 vParams;
out vec2 vUv;
out vec4 vFill;
out vec4 vFill2;
out vec4 vBorder;
out float vGrad;
void main() {
    gl_Position = vec4(aPos.x / uViewport.x * 2.0 - 1.0, 1.0 - aPos.y / uViewport.y * 2.0, 0.0, 1.0);
    vLocal = aLocal; vHalf = aHalf; vParams = aParams; vUv = aUv;
    vFill = aFill; vFill2 = aFill2; vBorder = aBorder; vGrad = aGrad;
}
)";

const char* kFragment = R"(#version 300 es
precision highp float;
in vec2 vLocal;
in vec2 vHalf;
in vec4 vParams;   // radius, border, kind, extra (shadow sigma)
in vec2 vUv;
in vec4 vFill;
in vec4 vFill2;
in vec4 vBorder;
in float vGrad;
uniform sampler2D uAtlas;
uniform sampler2D uImage;
out vec4 frag;

float sdRoundBox(vec2 p, vec2 b, float r) {
    r = min(r, min(b.x, b.y));
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Evan Wallace's closed-form rounded box shadow
float gaussian(float x, float sigma) { return exp(-(x * x) / (2.0 * sigma * sigma)) / (sqrt(2.0 * 3.14159265) * sigma); }
vec2 erf2(vec2 x) {
    vec2 s = sign(x), a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}
float shadowX(float x, float y, float sigma, float corner, vec2 halfSize) {
    float delta = min(halfSize.y - corner - abs(y), 0.0);
    float curved = halfSize.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    vec2 integral = 0.5 + 0.5 * erf2((x + vec2(-curved, curved)) * (sqrt(0.5) / sigma));
    return integral.y - integral.x;
}
float roundedBoxShadow(vec2 halfSize, vec2 p, float sigma, float corner) {
    corner = min(corner, min(halfSize.x, halfSize.y));
    float low = p.y - halfSize.y, high = p.y + halfSize.y;
    float start = clamp(-3.0 * sigma, low, high), end = clamp(3.0 * sigma, low, high);
    float step = (end - start) / 4.0;
    float y = start + step * 0.5;
    float value = 0.0;
    for (int i = 0; i < 4; i++) {
        value += shadowX(p.x, p.y - y, sigma, corner, halfSize) * gaussian(y, sigma) * step;
        y += step;
    }
    return value;
}

void main() {
    int kind = int(vParams.z + 0.5);
    float radius = vParams.x;
    float border = vParams.y;
    if (kind == 0) {
        float d = sdRoundBox(vLocal, vHalf, radius);
        float cov = clamp(0.5 - d, 0.0, 1.0);
        vec4 fill = mix(vFill, vFill2, clamp(vGrad, 0.0, 1.0));
        vec4 pf = vec4(fill.rgb * fill.a, fill.a);
        if (border > 0.0) {
            float inner = clamp(0.5 - (d + border), 0.0, 1.0);
            vec4 pb = vec4(vBorder.rgb * vBorder.a, vBorder.a);
            pf = mix(pb, pf, inner);
        }
        frag = pf * cov;
    } else if (kind == 1) {
        float a = roundedBoxShadow(vHalf, vLocal, max(vParams.w, 0.5), radius);
        frag = vec4(vFill.rgb * vFill.a, vFill.a) * a;
    } else if (kind == 2) {
        float a = texture(uAtlas, vUv).r;
        frag = vec4(vFill.rgb * vFill.a, vFill.a) * a;
    } else {
        vec4 t = texture(uImage, vUv);
        float cov = 1.0;
        if (radius > 0.0) cov = clamp(0.5 - sdRoundBox(vLocal, vHalf, radius), 0.0, 1.0);
        frag = t * vFill.a * cov;
    }
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[2048];
        glGetShaderInfoLog(s, sizeof buf, nullptr, buf);
        log::error("gl", "shader: {}", buf);
    }
    return s;
}

uint8_t u8(float v) { return uint8_t(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); }

} // namespace

Color Color::hex(std::string_view s, Color fallback) {
    if (s.starts_with('#')) s.remove_prefix(1);
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (char c : s)
        if (hv(c) < 0) return fallback;
    auto byte = [&](size_t i) { return float(hv(s[i]) * 16 + hv(s[i + 1])) / 255.f; };
    if (s.size() == 3) return {hv(s[0]) / 15.f, hv(s[1]) / 15.f, hv(s[2]) / 15.f, 1};
    if (s.size() == 6) return {byte(0), byte(2), byte(4), 1};
    if (s.size() == 8) return {byte(2), byte(4), byte(6), byte(0)};
    return fallback;
}

Color lerp(const Color& a, const Color& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

Renderer& Renderer::get() {
    static Renderer r;
    return r;
}

bool Renderer::init() {
    GLuint vs = compile(GL_VERTEX_SHADER, kVertex), fs = compile(GL_FRAGMENT_SHADER, kFragment);
    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[2048];
        glGetProgramInfoLog(m_program, sizeof buf, nullptr, buf);
        log::error("gl", "link: {}", buf);
        return false;
    }
    glUseProgram(m_program);
    m_uViewport = glGetUniformLocation(m_program, "uViewport");
    glUniform1i(glGetUniformLocation(m_program, "uAtlas"), 0);
    glUniform1i(glGetUniformLocation(m_program, "uImage"), 1);

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ibo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    auto f = [](GLuint loc, int n, size_t off) {
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, n, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(off));
    };
    auto c = [](GLuint loc, size_t off) {
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(off));
    };
    f(0, 2, offsetof(Vertex, px));
    f(1, 2, offsetof(Vertex, lx));
    f(2, 2, offsetof(Vertex, hx));
    f(3, 4, offsetof(Vertex, radius));
    f(4, 2, offsetof(Vertex, u));
    c(5, offsetof(Vertex, fill));
    c(6, offsetof(Vertex, fill2));
    c(7, offsetof(Vertex, border_c));
    f(8, 1, offsetof(Vertex, grad));

    uint32_t white = 0xffffffff;
    glGenTextures(1, &m_white);
    glBindTexture(GL_TEXTURE_2D, m_white);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    return true;
}

void Renderer::beginFrame(int bw, int bh, float deviceScale) {
    m_bw = bw;
    m_bh = bh;
    m_deviceScale = deviceScale;
    m_state.clear();
    m_state.push_back({deviceScale, 0, 0, deviceScale, 0, 0, 1.f, -1});
    m_clips.clear();
    m_clipStack.clear();
    m_verts.clear();
    m_indices.clear();
    m_draws.clear();
    glViewport(0, 0, bw, bh);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::endFrame() { flush(); }

void Renderer::save() { m_state.push_back(m_state.back()); }
void Renderer::restore() {
    if (m_state.size() > 1) m_state.pop_back();
}

void Renderer::translate(float x, float y) {
    auto& s = m_state.back();
    s.tx += s.a * x + s.c * y;
    s.ty += s.b * x + s.d * y;
}

void Renderer::scaleAbout(float k, float cx, float cy) {
    translate(cx, cy);
    auto& s = m_state.back();
    s.a *= k; s.b *= k; s.c *= k; s.d *= k;
    translate(-cx, -cy);
}

void Renderer::rotateAbout(float deg, float cx, float cy) {
    translate(cx, cy);
    auto& s = m_state.back();
    float rad = deg * float(M_PI) / 180.f, cs = std::cos(rad), sn = std::sin(rad);
    float a = s.a * cs + s.c * sn, b = s.b * cs + s.d * sn;
    float c = -s.a * sn + s.c * cs, d = -s.b * sn + s.d * cs;
    s.a = a; s.b = b; s.c = c; s.d = d;
    translate(-cx, -cy);
}

void Renderer::multiplyOpacity(float a) { m_state.back().opacity *= a; }

float Renderer::pixelScale() const {
    auto& s = m_state.back();
    return std::sqrt(std::fabs(s.a * s.d - s.b * s.c));
}

void Renderer::mapToBuffer(float x, float y, float& bx, float& by) const {
    auto& s = m_state.back();
    bx = s.a * x + s.c * y + s.tx;
    by = s.b * x + s.d * y + s.ty;
}

void Renderer::pushClip(RectF r) {
    float x0, y0, x1, y1;
    mapToBuffer(r.x, r.y, x0, y0);
    mapToBuffer(r.x + r.w, r.y + r.h, x1, y1);
    Clip c{int(std::floor(std::min(x0, x1))), int(std::floor(std::min(y0, y1))), 0, 0};
    c.w = int(std::ceil(std::max(x0, x1))) - c.x;
    c.h = int(std::ceil(std::max(y0, y1))) - c.y;
    int cur = m_state.back().clip;
    if (cur >= 0) {   // intersect
        auto& p = m_clips[size_t(cur)];
        int nx = std::max(c.x, p.x), ny = std::max(c.y, p.y);
        int nx1 = std::min(c.x + c.w, p.x + p.w), ny1 = std::min(c.y + c.h, p.y + p.h);
        c = {nx, ny, std::max(0, nx1 - nx), std::max(0, ny1 - ny)};
    }
    m_clips.push_back(c);
    m_clipStack.push_back(cur);
    m_state.back().clip = int(m_clips.size()) - 1;
}

void Renderer::popClip() {
    if (m_clipStack.empty()) return;
    m_state.back().clip = m_clipStack.back();
    m_clipStack.pop_back();
}

void Renderer::beginDraw(GLuint atlas, GLuint image) {
    int clip = m_state.back().clip;
    if (!m_draws.empty()) {
        auto& d = m_draws.back();
        bool atlasOk = atlas == 0 || d.atlas == 0 || d.atlas == atlas;
        bool imageOk = image == 0 || d.image == 0 || d.image == image;
        if (atlasOk && imageOk && d.clip == clip) {
            if (atlas) d.atlas = atlas;
            if (image) d.image = image;
            return;
        }
    }
    m_draws.push_back({atlas, image, clip, uint32_t(m_indices.size()), 0});
}

void Renderer::quad(float x0, float y0, float x1, float y1, const State& s, float kind, float radius, float border, float extra, Color fill,
                    Color fill2, Color bord, bool gradient, bool horizontal, float u0, float v0, float u1, float v1) {
    float k = std::sqrt(std::fabs(s.a * s.d - s.b * s.c));   // uniform scale for SDF params
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    float hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
    fill.a *= s.opacity;
    fill2.a *= s.opacity;
    bord.a *= s.opacity;
    uint32_t base = uint32_t(m_verts.size());
    const float lxs[4] = {x0, x1, x1, x0}, lys[4] = {y0, y0, y1, y1};
    const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
    for (int i = 0; i < 4; i++) {
        Vertex v{};
        v.px = s.a * lxs[i] + s.c * lys[i] + s.tx;
        v.py = s.b * lxs[i] + s.d * lys[i] + s.ty;
        v.lx = (lxs[i] - cx) * k;
        v.ly = (lys[i] - cy) * k;
        v.hx = hx * k;
        v.hy = hy * k;
        v.radius = radius * k;
        v.border = border * k;
        v.kind = kind;
        v.extra = extra * k;
        v.u = us[i];
        v.v = vs[i];
        v.fill[0] = u8(fill.r); v.fill[1] = u8(fill.g); v.fill[2] = u8(fill.b); v.fill[3] = u8(fill.a);
        Color f2 = gradient ? fill2 : fill;
        v.fill2[0] = u8(f2.r); v.fill2[1] = u8(f2.g); v.fill2[2] = u8(f2.b); v.fill2[3] = u8(f2.a);
        v.border_c[0] = u8(bord.r); v.border_c[1] = u8(bord.g); v.border_c[2] = u8(bord.b); v.border_c[3] = u8(bord.a);
        v.grad = gradient ? (horizontal ? (i == 1 || i == 2 ? 1.f : 0.f) : (i >= 2 ? 1.f : 0.f)) : 0.f;
        m_verts.push_back(v);
    }
    for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) m_indices.push_back(base + i);
    m_draws.back().count += 6;
}

void Renderer::rect(RectF r, const Box& b) {
    auto& s = m_state.back();
    if (s.opacity <= 0.001f || r.w <= 0 || r.h <= 0) return;
    if (b.fill.a <= 0.001f && (b.border <= 0 || b.borderColor.a <= 0.001f) && (!b.gradient || b.fill2.a <= 0.001f)) return;
    beginDraw(0, 0);
    quad(r.x, r.y, r.x + r.w, r.y + r.h, s, 0, b.radius, b.border, 0, b.fill, b.fill2, b.borderColor, b.gradient, b.horizontal);
}

void Renderer::shadow(RectF r, float radius, float blur, Color c) {
    auto& s = m_state.back();
    if (s.opacity <= 0.001f || c.a <= 0.001f) return;
    float sigma = std::max(0.5f, blur * 0.5f);
    float pad = sigma * 3.f;
    beginDraw(0, 0);
    // the quad covers the spread; the SDF half-size is the box itself, so pass it via local coords
    State st = s;
    float x0 = r.x - pad, y0 = r.y - pad, x1 = r.x + r.w + pad, y1 = r.y + r.h + pad;
    float k = std::sqrt(std::fabs(st.a * st.d - st.b * st.c));
    uint32_t base = uint32_t(m_verts.size());
    const float lxs[4] = {x0, x1, x1, x0}, lys[4] = {y0, y0, y1, y1};
    float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    Color f = c;
    f.a *= st.opacity;
    for (int i = 0; i < 4; i++) {
        Vertex v{};
        v.px = st.a * lxs[i] + st.c * lys[i] + st.tx;
        v.py = st.b * lxs[i] + st.d * lys[i] + st.ty;
        v.lx = (lxs[i] - cx) * k;
        v.ly = (lys[i] - cy) * k;
        v.hx = r.w * 0.5f * k;
        v.hy = r.h * 0.5f * k;
        v.radius = radius * k;
        v.kind = 1;
        v.extra = sigma * k;
        v.fill[0] = u8(f.r); v.fill[1] = u8(f.g); v.fill[2] = u8(f.b); v.fill[3] = u8(f.a);
        m_verts.push_back(v);
    }
    for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) m_indices.push_back(base + i);
    m_draws.back().count += 6;
}

void Renderer::image(const Texture& t, RectF dst, float radius, RectF uv) {
    auto& s = m_state.back();
    if (!t.id || s.opacity <= 0.001f) return;
    beginDraw(0, t.id);
    quad(dst.x, dst.y, dst.x + dst.w, dst.y + dst.h, s, 3, radius, 0, 0, Color{1, 1, 1, 1}, {}, {}, false, false, uv.x, uv.y, uv.x + uv.w,
         uv.y + uv.h);
}

void Renderer::glyph(GLuint atlas, float bx, float by, float bw, float bh, float u0, float v0, float u1, float v1, Color c) {
    auto& s = m_state.back();
    if (s.opacity <= 0.001f || c.a <= 0.001f) return;
    beginDraw(atlas, 0);
    State id{1, 0, 0, 1, 0, 0, s.opacity, s.clip};   // already in buffer pixels
    quad(bx, by, bx + bw, by + bh, id, 2, 0, 0, 0, c, {}, {}, false, false, u0, v0, u1, v1);
}

void Renderer::flush() {
    if (m_indices.empty()) return;
    glUseProgram(m_program);
    glUniform2f(m_uViewport, float(m_bw), float(m_bh));
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    size_t vbytes = m_verts.size() * sizeof(Vertex);
    if (vbytes > m_vboCap) {
        m_vboCap = std::max(vbytes, m_vboCap * 2);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m_vboCap), nullptr, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(vbytes), m_verts.data());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    size_t ibytes = m_indices.size() * sizeof(uint32_t);
    if (ibytes > m_iboCap) {
        m_iboCap = std::max(ibytes, m_iboCap * 2);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(m_iboCap), nullptr, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, GLsizeiptr(ibytes), m_indices.data());

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // premultiplied
    for (auto& d : m_draws) {
        if (!d.count) continue;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, d.atlas ? d.atlas : m_white);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, d.image ? d.image : m_white);
        if (d.clip >= 0) {
            auto& c = m_clips[size_t(d.clip)];
            glEnable(GL_SCISSOR_TEST);
            glScissor(c.x, m_bh - (c.y + c.h), std::max(0, c.w), std::max(0, c.h));
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
        glDrawElements(GL_TRIANGLES, GLsizei(d.count), GL_UNSIGNED_INT, reinterpret_cast<void*>(size_t(d.first) * sizeof(uint32_t)));
    }
    glDisable(GL_SCISSOR_TEST);
}

} // namespace ks::gfx
