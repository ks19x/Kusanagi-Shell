#include "shell/bar/widgets/kusanagi_box.h"

#include "render/animation/animation_manager.h"
#include "render/core/render_styles.h"
#include "render/scene/node.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/palette.h"

#include <cmath>

using json = nlohmann::json;

namespace {

  bool visible(const json& c) {
    if (c.is_array()) return !c.empty();
    if (!c.is_string()) return false;
    const auto s = c.get<std::string>();
    return !s.empty() && s != "transparent" && s != "none";
  }

  std::string tok(const json& j) { return j.is_string() ? j.get<std::string>() : std::string(); }

  // The first or last colour of a colour token or a [c0, c1] gradient.
  json firstOf(const json& f) { return f.is_array() ? (f.empty() ? json("transparent") : f.front()) : f; }
  json lastOf(const json& f) { return f.is_array() ? (f.empty() ? json("transparent") : f.back()) : f; }

  Color resolved(const json& token) { return resolveColorSpec(kusanagi_bar::color(tok(token), clearColorSpec())); }

  std::map<std::string, json>& looks() {
    static std::map<std::string, json> m;
    return m;
  }

} // namespace

namespace kusanagi_bar {
  void setLooks(std::map<std::string, json> l) { looks() = std::move(l); }
  const json* look(const std::string& key) {
    const auto it = looks().find(key);
    return it != looks().end() ? &it->second : nullptr;
  }
  ColorSpec color(const std::string& token, const ColorSpec& fallback) {
    const auto slash = token.rfind('/');
    if (token.substr(0, slash == std::string::npos || slash == 0 ? token.size() : slash) != "warn") {
      return kusanagi::color(token, fallback);
    }
    float a = 1.0F;
    if (slash != std::string::npos && slash > 0) {
      try {
        a = std::stof(token.substr(slash + 1));
      } catch (...) {
      }
    }
    const Color d = resolveColorSpec(colorSpecFromRole(ColorRole::Error));
    return ColorSpec{.role = std::nullopt, .fixed = Color{0.6F + 0.4F * d.r, 0.468F + 0.4F * d.g, 0.06F + 0.4F * d.b, 1.0F}, .alpha = a};
  }
} // namespace kusanagi_bar

KusanagiBox::~KusanagiBox() {
  if (m_animations != nullptr) m_animations->cancelForOwner(this);
}

bool KusanagiBox::draws(const json& s) {
  auto has = [&](const char* k) { return s.contains(k) && visible(s[k]); };
  const auto cap = [&](const char* k) { return s.contains(k) && s[k].is_string() && s[k] != "none"; };
  return has("bg") || has("hoverBg") || (s.value("borderWidth", 0.0) > 0 && has("border")) || (s.contains("line") && s["line"].is_object())
      || cap("capStart") || cap("capEnd");
}

float KusanagiBox::capSize(const std::string& k, float cross) {
  return k == "arrow" || k == "arrow-in" || k == "slant" || k == "slant-back" ? std::round(cross / 2.0F) : 0.0F;
}

std::unique_ptr<Node> KusanagiBox::create() {
  auto root = ui::node({});
  m_root = root.get();
  // Only a look: pointer hits go to the input areas around and under it.
  root->setHitTestVisible(false);
  root->addChild(ui::box({.out = &m_body}));
  for (Cap* cap : {&m_start, &m_end}) {
    auto node = ui::node({});
    cap->node = node.get();
    node->addChild(ui::box({.out = &cap->back}));
    for (auto& part : cap->parts) {
      auto clip = ui::node({});
      part.clip = clip.get();
      clip->setClipChildren(true);
      clip->addChild(ui::box({.out = &part.fill}));
      node->addChild(std::move(clip));
    }
    root->addChild(std::move(node));
  }
  root->addChild(ui::box({.out = &m_line}));
  return root;
}

// One cap, in the bar's own direction (x along, y across); `atEnd` mirrors it. Each triangle part is the
// clip rect intersected with the side of the edge P-Q that holds a reference point. A big rotated box
// covers that side.
void KusanagiBox::layoutCap(Cap& cap, const std::string& kind, bool atEnd, bool vertical, float c, float h, const json& paint,
                            const json& back) {
  struct Part {
    float rx, ry, rw, rh;     // clip rect
    float px, py, qx, qy;     // the edge
    float ix, iy;             // a point on the filled side
  };
  std::vector<Part> parts;
  const float m = h / 2.0F;
  if (kind == "arrow") {
    parts = {{0, 0, c, m, 0, m, c, 0, c, m - 0.5F}, {0, m, c, h - m, 0, m, c, h, c, m + 0.5F}};
  } else if (kind == "arrow-in") {
    parts = {{0, 0, c, m, 0, 0, c, m, c, 0.5F}, {0, m, c, h - m, 0, h, c, m, c, h - 0.5F}};
  } else if (kind == "slant") {
    parts = {{0, 0, c, h, 0, h, c, 0, c, h - 0.5F}};
  } else if (kind == "slant-back") {
    parts = {{0, 0, c, h, 0, 0, c, h, c, 0.5F}};
  }
  // Bar direction to screen: mirror for the end cap, swap axes on side bars.
  auto map = [&](float x, float y) {
    if (atEnd) x = c - x;
    return vertical ? std::pair<float, float>{y, x} : std::pair<float, float>{x, y};
  };
  auto mapRect = [&](float x, float y, float w, float hh) {
    auto [x0, y0] = map(x, y);
    auto [x1, y1] = map(x + w, y + hh);
    return std::array<float, 4>{std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0)};
  };

  // Matches the classic look: a cap with a visible capStartBg / capEndBg shows only that colour, and the
  // arrow is hidden behind it.
  const bool backOn = resolved(back).a > 0.0F;
  cap.back->setVisible(backOn);
  const auto box = mapRect(0, 0, c, h);
  cap.back->setPosition(box[0], box[1]);
  cap.back->setSize(box[2], box[3]);
  if (backOn) cap.back->setFill(kusanagi_bar::color(tok(back), clearColorSpec()));

  const ColorSpec fill = kusanagi_bar::color(tok(paint), clearColorSpec());
  const float big = 4.0F * (c + h);
  for (std::size_t i = 0; i < cap.parts.size(); ++i) {
    auto& part = cap.parts[i];
    part.clip->setVisible(i < parts.size() && !backOn);
    if (i >= parts.size() || backOn) continue;
    const Part& p = parts[i];
    const auto r = mapRect(p.rx, p.ry, p.rw, p.rh);
    part.clip->setPosition(r[0], r[1]);
    part.clip->setSize(r[2], r[3]);
    auto [px, py] = map(p.px, p.py);
    auto [qx, qy] = map(p.qx, p.qy);
    auto [ix, iy] = map(p.ix, p.iy);
    // The box's local y-axis (its "inside") must point at the reference point.
    float dx = qx - px, dy = qy - py;
    const float len = std::hypot(dx, dy);
    dx /= len;
    dy /= len;
    if ((ix - px) * -dy + (iy - py) * dx < 0) {
      std::swap(px, qx);
      std::swap(py, qy);
      dx = -dx;
      dy = -dy;
    }
    // The box starts `big / 2` before P along the edge, in clip-local coordinates.
    const float sx = px - r[0] - dx * big / 2.0F;
    const float sy = py - r[1] - dy * big / 2.0F;
    part.fill->setSize(big, big);
    part.fill->setTransformOrigin(0.0F, 0.0F);
    part.fill->setRotation(std::atan2(dy, dx));
    part.fill->setPosition(sx, sy);
    part.fill->setFill(fill);
  }
}

void KusanagiBox::reapply() {
  const Last last = m_last;
  apply(last.eff, last.hovered, last.vertical, last.x, last.y, last.w, last.h, last.scale);
}

void KusanagiBox::apply(const json& eff, bool hovered, bool vertical, float x, float y, float w, float h, float scale) {
  if (m_root == nullptr) return;
  if (&eff != &m_last.eff) m_last = Last{eff, hovered, vertical, x, y, w, h, scale};
  const bool on = draws(eff);
  m_root->setVisible(on);
  if (!on) return;
  m_root->setPosition(x, y);
  m_root->setSize(w, h);

  const float cross = vertical ? w : h;
  const std::string capStart = eff.value("capStart", std::string("none"));
  const std::string capEnd = eff.value("capEnd", std::string("none"));
  const float cs = capSize(capStart, cross), ce = capSize(capEnd, cross);
  const json fill = hovered && eff.contains("hoverBg") && visible(eff["hoverBg"]) ? eff["hoverBg"] : eff.value("bg", json("transparent"));

  const float bx = vertical ? 0.0F : cs, by = vertical ? cs : 0.0F;
  const float bw = vertical ? w : std::max(0.0F, w - cs - ce), bh = vertical ? std::max(0.0F, h - cs - ce) : h;
  m_body->setPosition(bx, by);
  m_body->setSize(bw, bh);
  auto corner = [&](int i) {
    const bool atStart = vertical ? (i == 0 || i == 1) : (i == 0 || i == 3);
    const std::string& cap = atStart ? capStart : capEnd;
    if (cap == "round") return std::floor(cross / 2.0F);
    if (cap != "none") return 0.0F;
    const json& r = eff.value("radius", json(0));
    if (r.is_array()) return (static_cast<std::size_t>(i) < r.size() && r[i].is_number() ? r[i].get<float>() : 0.0F) * scale;
    return (r.is_number() ? r.get<float>() : 0.0F) * scale;
  };
  RoundedRectStyle style;
  style.radius = Radii(corner(0), corner(1), corner(2), corner(3));
  const float bwidth = eff.value("borderWidth", 0.0F);
  style.borderWidth = visible(eff.value("border", json())) ? bwidth * scale : 0.0F;
  style.border = resolved(eff.value("border", json("transparent")));
  if (fill.is_array() && fill.size() > 1) {
    style.fillMode = FillMode::LinearGradient;
    style.gradientDirection = vertical ? GradientDirection::Vertical : GradientDirection::Horizontal;
    const Color c0 = resolved(fill.front()), c1 = resolved(fill.back());
    style.gradientStops = {GradientStop{0.0F, c0}, GradientStop{1.0F, c1}, GradientStop{1.0F, c1}, GradientStop{1.0F, c1}};
    style.fill = c0;
  } else {
    style.fillMode = FillMode::Solid;
    style.fill = resolved(firstOf(fill));
    // A fixed-length fade from whatever colour is showing now.
    const Color target = style.fill;
    if (!m_hasFill || m_animations == nullptr) {
      m_shownFill = m_targetFill = target;
      m_hasFill = true;
    } else if (!(target == m_targetFill)) {
      m_targetFill = target;
      const Color from = m_shownFill;
      m_animations->cancelForOwner(this);
      m_animations->animateTimer(0.0F, 1.0F, 160.0F, Easing::Linear, [this, from, target](float t) {
        m_shownFill = lerpColor(from, target, t);
        m_style.fill = m_shownFill;
        m_body->setStyle(m_style);
        if (m_redraw) m_redraw();
      }, {}, this);
    }
    style.fill = m_shownFill;
  }
  m_style = style;
  m_body->setStyle(style);

  // Caps are painted in the body's first and last colour.
  m_start.node->setVisible(cs > 0);
  m_end.node->setVisible(ce > 0);
  if (cs > 0) {
    m_start.node->setPosition(0.0F, 0.0F);
    layoutCap(m_start, capStart, false, vertical, cs, cross, firstOf(fill), eff.value("capStartBg", json()));
  }
  if (ce > 0) {
    m_end.node->setPosition(vertical ? 0.0F : w - ce, vertical ? h - ce : 0.0F);
    layoutCap(m_end, capEnd, true, vertical, ce, cross, lastOf(fill), eff.value("capEndBg", json()));
  }

  // Indicator line
  const json line = eff.value("line", json());
  m_line->setVisible(line.is_object() && line.value("width", 2.0) > 0);
  if (line.is_object()) {
    const std::string pos = line.value("pos", std::string("bottom"));
    const float lw = line.value("width", 2.0F) * scale;
    const float frac = line.value("length", 1.0F);
    const bool horiz = pos == "top" || pos == "bottom";
    const float inner = (horiz ? w : h) - cs - ce;
    const float lx = pos == "right" ? w - lw : horiz ? cs + (inner - inner * frac) / 2.0F : 0.0F;
    const float ly = pos == "bottom" ? h - lw : horiz ? 0.0F : cs + (inner - inner * frac) / 2.0F;
    m_line->setPosition(lx, ly);
    m_line->setSize(horiz ? inner * frac : lw, horiz ? lw : inner * frac);
    m_line->setRadius(line.value("round", false) ? lw / 2.0F : 0.0F);
    m_line->setFill(kusanagi_bar::color(line.contains("color") ? tok(line["color"]) : "accent", colorSpecFromRole(ColorRole::Primary)));
  }
}
