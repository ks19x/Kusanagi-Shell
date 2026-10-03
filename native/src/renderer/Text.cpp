#include "renderer/Text.hpp"
#include "app/Log.hpp"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include <hb-ft.h>
#include <hb.h>

#include <cmath>
#include <map>
#include <unordered_map>

namespace ks::gfx {

struct Face {
    FT_Face ft = nullptr;
    hb_font_t* hb = nullptr;
    bool embolden = false;
    int ascent = 0, descent = 0;   // buffer px
};

namespace {

FT_Library g_ft = nullptr;

struct FontFile {
    std::string path;
    int index = 0;
    bool embolden = false;
};

// family+bold -> ordered list of files: the match first, then fontconfig's fallback sort
std::map<std::pair<std::string, bool>, std::vector<FontFile>> g_chains;
std::map<std::tuple<std::string, int, bool, int>, std::unique_ptr<Face>> g_faces;

const std::vector<FontFile>& chain(const std::string& family, bool bold) {
    auto key = std::make_pair(family, bold);
    if (auto it = g_chains.find(key); it != g_chains.end()) return it->second;
    static bool inited = FcInit();
    (void)inited;
    std::vector<FontFile> files;
    FcPattern* pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str()));
    FcPatternAddInteger(pat, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res;
    FcFontSet* set = FcFontSort(nullptr, pat, FcTrue, nullptr, &res);
    if (set) {
        for (int i = 0; i < set->nfont && files.size() < 24; i++) {
            FcPattern* f = FcFontRenderPrepare(nullptr, pat, set->fonts[i]);
            FcChar8* file = nullptr;
            int index = 0;
            FcBool emb = FcFalse;
            if (FcPatternGetString(f, FC_FILE, 0, &file) == FcResultMatch) {
                FcPatternGetInteger(f, FC_INDEX, 0, &index);
                FcPatternGetBool(f, FC_EMBOLDEN, 0, &emb);
                files.push_back({reinterpret_cast<char*>(file), index, bool(emb)});
            }
            FcPatternDestroy(f);
        }
        FcFontSetDestroy(set);
    }
    FcPatternDestroy(pat);
    if (files.empty()) log::warn("text", "no font for '{}'", family);
    return g_chains.emplace(key, std::move(files)).first->second;
}

Face* face(const FontFile& f, int px) {
    auto key = std::make_tuple(f.path, f.index, f.embolden, px);
    if (auto it = g_faces.find(key); it != g_faces.end()) return it->second.get();
    if (!g_ft) FT_Init_FreeType(&g_ft);
    auto fc = std::make_unique<Face>();
    if (FT_New_Face(g_ft, f.path.c_str(), f.index, &fc->ft) != 0) {
        g_faces.emplace(key, nullptr);
        return nullptr;
    }
    if (FT_IS_SCALABLE(fc->ft)) FT_Set_Pixel_Sizes(fc->ft, 0, FT_UInt(px));
    else if (fc->ft->num_fixed_sizes > 0) FT_Select_Size(fc->ft, 0);
    fc->embolden = f.embolden;
    fc->hb = hb_ft_font_create_referenced(fc->ft);
    hb_ft_font_set_load_flags(fc->hb, FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL);
    fc->ascent = int(std::ceil(fc->ft->size->metrics.ascender / 64.0));
    fc->descent = int(std::ceil(-fc->ft->size->metrics.descender / 64.0));
    auto* raw = fc.get();
    g_faces.emplace(key, std::move(fc));
    return raw;
}

// ---- glyph atlas: R8 pages, shelf packing
struct Slot {
    GLuint tex;
    float u0, v0, u1, v1;
    int w, h, left, top;
};
struct Page {
    GLuint tex;
    int shelfY = 0, shelfH = 0, x = 0;
};
constexpr int kPage = 1024;
std::vector<Page> g_pages;
std::unordered_map<uint64_t, Slot> g_glyphs;   // (face ptr ^ gid)

Page& newPage() {
    Page p{};
    glGenTextures(1, &p.tex);
    glBindTexture(GL_TEXTURE_2D, p.tex);
    std::vector<uint8_t> zero(size_t(kPage) * kPage, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kPage, kPage, 0, GL_RED, GL_UNSIGNED_BYTE, zero.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_pages.push_back(p);
    return g_pages.back();
}

const Slot* glyphSlot(Face* f, uint32_t gid) {
    uint64_t key = (uint64_t(reinterpret_cast<uintptr_t>(f)) << 20) ^ gid;
    if (auto it = g_glyphs.find(key); it != g_glyphs.end()) return &it->second;
    if (FT_Load_Glyph(f->ft, gid, FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL) != 0) return nullptr;
    if (f->embolden) FT_GlyphSlot_Embolden(f->ft->glyph);
    if (FT_Render_Glyph(f->ft->glyph, FT_RENDER_MODE_NORMAL) != 0) return nullptr;
    auto& bm = f->ft->glyph->bitmap;
    Slot s{};
    s.w = int(bm.width);
    s.h = int(bm.rows);
    s.left = f->ft->glyph->bitmap_left;
    s.top = f->ft->glyph->bitmap_top;
    if (s.w > 0 && s.h > 0 && bm.pixel_mode == FT_PIXEL_MODE_GRAY) {
        if (g_pages.empty()) newPage();
        Page* p = &g_pages.back();
        if (p->x + s.w + 1 > kPage) { p->shelfY += p->shelfH + 1; p->shelfH = 0; p->x = 0; }
        if (p->shelfY + s.h + 1 > kPage) p = &newPage();
        int x = p->x, y = p->shelfY;
        glBindTexture(GL_TEXTURE_2D, p->tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, bm.pitch);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, s.w, s.h, GL_RED, GL_UNSIGNED_BYTE, bm.buffer);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        p->x += s.w + 1;
        p->shelfH = std::max(p->shelfH, s.h);
        s.tex = p->tex;
        s.u0 = float(x) / kPage;
        s.v0 = float(y) / kPage;
        s.u1 = float(x + s.w) / kPage;
        s.v1 = float(y + s.h) / kPage;
    } else {
        s.w = s.h = 0;
    }
    return &g_glyphs.emplace(key, s).first->second;
}

uint32_t nextCodepoint(const std::string& s, size_t& i) {
    auto c = uint8_t(s[i]);
    uint32_t cp;
    int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
    else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
    else { cp = c & 0x07; n = 4; }
    for (int k = 1; k < n && i + size_t(k) < s.size(); k++) cp = (cp << 6) | (uint8_t(s[i + size_t(k)]) & 0x3F);
    i += size_t(n);
    return cp;
}

} // namespace

void TextLayout::set(const std::string& text, const FontSpec& font, float scale) {
    if (text == m_text && font == m_font && scale == m_scale && !m_glyphs.empty() == !text.empty()) return;
    m_text = text;
    m_font = font;
    m_scale = scale;
    shape();
    if (m_elide > 0) elide(m_elide);
}

void TextLayout::shape() {
    m_glyphs.clear();
    m_width = 0;
    int px = std::max(1, int(std::lround(m_font.px * m_scale)));
    auto& files = chain(m_font.family, m_font.bold);
    if (files.empty()) return;
    Face* primary = face(files[0], px);
    if (!primary) return;
    m_ascent = primary->ascent;
    m_descent = primary->descent;
    if (m_text.empty()) return;

    // split into runs by the first face in the chain that has each codepoint
    auto pick = [&](uint32_t cp) -> Face* {
        for (auto& f : files) {
            Face* fc = face(f, px);
            if (fc && FT_Get_Char_Index(fc->ft, cp)) return fc;
        }
        return primary;
    };
    struct Run { Face* f; size_t start, end; };
    std::vector<Run> runs;
    for (size_t i = 0; i < m_text.size();) {
        size_t start = i;
        uint32_t cp = nextCodepoint(m_text, i);
        Face* f = (cp == ' ' || cp == '\t') && !runs.empty() ? runs.back().f : pick(cp);
        if (!runs.empty() && runs.back().f == f && runs.back().end == start) runs.back().end = i;
        else runs.push_back({f, start, i});
    }
    int pen = 0;
    hb_buffer_t* buf = hb_buffer_create();
    for (auto& r : runs) {
        hb_buffer_reset(buf);
        hb_buffer_add_utf8(buf, m_text.data(), int(m_text.size()), unsigned(r.start), int(r.end - r.start));
        hb_buffer_guess_segment_properties(buf);
        hb_shape(r.f->hb, buf, nullptr, 0);
        unsigned n = 0;
        auto* info = hb_buffer_get_glyph_infos(buf, &n);
        auto* pos = hb_buffer_get_glyph_positions(buf, &n);
        for (unsigned k = 0; k < n; k++) {
            m_glyphs.push_back({r.f, info[k].codepoint, pen + int(std::lround(pos[k].x_offset / 64.0)), -int(std::lround(pos[k].y_offset / 64.0))});
            pen += int(std::lround(pos[k].x_advance / 64.0));
        }
    }
    hb_buffer_destroy(buf);
    m_width = pen;
}

void TextLayout::elide(float maxW) {
    m_elide = maxW;
    if (maxW <= 0 || width() <= maxW || m_glyphs.empty()) return;
    int limit = int(maxW * m_scale);
    Face* f = m_glyphs.front().face;
    uint32_t ell = FT_Get_Char_Index(f->ft, 0x2026);
    FT_Load_Glyph(f->ft, ell, FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL);
    int ellW = int(std::lround(f->ft->glyph->advance.x / 64.0));
    size_t keep = 0;
    for (size_t i = 0; i < m_glyphs.size(); i++) {
        int next = i + 1 < m_glyphs.size() ? m_glyphs[i + 1].x : m_width;
        if (next + ellW > limit) break;
        keep = i + 1;
    }
    int x = keep < m_glyphs.size() ? m_glyphs[keep].x : m_width;
    m_glyphs.resize(keep);
    m_glyphs.push_back({f, ell, x, 0});
    m_width = x + ellW;
}

void TextLayout::draw(Renderer& r, float x, float y, Color c) const {
    if (m_glyphs.empty()) return;
    float bx, by;
    r.mapToBuffer(x, y, bx, by);
    float k = r.pixelScale() / m_scale;   // ≠1 only mid scale-animation
    int ox = int(std::lround(bx)), base = int(std::lround(by)) + int(std::lround(m_ascent * k));
    for (auto& g : m_glyphs) {
        const Slot* s = glyphSlot(g.face, g.gid);
        if (!s || !s->w) continue;
        float gx = ox + (g.x + s->left) * k, gy = base + (g.dy - s->top) * k;
        r.glyph(s->tex, std::round(gx), std::round(gy), s->w * k, s->h * k, s->u0, s->v0, s->u1, s->v1, c);
    }
}

float textWidth(const std::string& text, const FontSpec& font, float scale) {
    TextLayout l;
    l.set(text, font, scale);
    return l.width();
}

} // namespace ks::gfx
