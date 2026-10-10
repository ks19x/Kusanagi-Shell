#include "shell/kusanagi/panel/cp_controls.h"

#include "core/deferred_call.h"
#include "core/process/process.h"
#include "cursor-shape-v1-client-protocol.h"
#include "render/animation/animation_manager.h"
#include "render/core/color.h"
#include "render/core/renderer.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <cmath>
#include <wayland-client-protocol.h>

namespace kusanagi::cp {

  namespace {
    constexpr auto kPointer = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;

    std::string escapeMarkup(const std::string& s) {
      std::string out;
      out.reserve(s.size());
      for (const char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        default: out += c;
        }
      }
      return out;
    }

    Box* addBox(Node& parent) { return static_cast<Box*>(parent.addChild(ui::box({}))); }

    // Press feedback shared by the buttons: shrink while held, spring back on release.
    void pressFeedback(InputArea& area, float pressedScale) {
      area.setOnPress([&area, pressedScale](const InputArea::PointerData& d) {
        tweenScale(area, d.pressed ? pressedScale : 1.0F, 160);
      });
      area.setOnCancel([&area]() { tweenScale(area, 1.0F, 160); });
    }
  } // namespace

  std::string utf8(char32_t c) {
    std::string out;
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
  }

  std::unique_ptr<Label> text(const std::string& s, float px, bool bold, const ColorSpec& color) {
    return ui::label({
        .text = s,
        .fontSize = px,
        .fontWeight = bold ? FontWeight::Bold : FontWeight::Normal,
        .fontFamily = kusanagi::font(),
        .color = color,
        .baselineMode = LabelBaselineMode::FontLine,
    });
  }

  std::unique_ptr<Label> icon(char32_t codepoint, float px, const ColorSpec& color) {
    return ui::label({
        .text = codepoint != 0 ? utf8(codepoint) : std::string(),
        .fontSize = px,
        .fontFamily = std::string(kIconFont),
        .color = color,
        .baselineMode = LabelBaselineMode::FontLine,
    });
  }

  void setSpacedText(Label& label, const std::string& s, float letterSpacingPx) {
    // Pango letter_spacing is in 1/1024 pt, and at 96 dpi a px is 0.75 pt.
    const int spacing = static_cast<int>(std::lround(letterSpacingPx * 0.75F * 1024.0F));
    label.setUseMarkup(true);
    label.setText("<span letter_spacing=\"" + std::to_string(spacing) + "\">" + escapeMarkup(s) + "</span>");
  }

  float cardRadius() { return std::max(6.0F, kusanagi::radius() - 6.0F); }

  float bezier(float x1, float y1, float x2, float y2, float t) {
    t = std::clamp(t, 0.0F, 1.0F);
    // Solve x(u) = t for u (Newton, with bisection as a fallback) and return y(u).
    const auto curve = [](float a, float b, float u) {
      const float v = 1.0F - u;
      return 3.0F * v * v * u * a + 3.0F * v * u * u * b + u * u * u;
    };
    const auto slope = [](float a, float b, float u) {
      const float v = 1.0F - u;
      return 3.0F * v * v * a + 6.0F * v * u * (b - a) + 3.0F * u * u * (1.0F - b);
    };
    float u = t;
    for (int i = 0; i < 8; ++i) {
      const float err = curve(x1, x2, u) - t;
      if (std::fabs(err) < 1e-5F) return curve(y1, y2, u);
      const float d = slope(x1, x2, u);
      if (std::fabs(d) < 1e-6F) break;
      u = std::clamp(u - err / d, 0.0F, 1.0F);
    }
    float lo = 0.0F;
    float hi = 1.0F;
    u = t;
    for (int i = 0; i < 24; ++i) {
      const float x = curve(x1, x2, u);
      if (std::fabs(x - t) < 1e-5F) break;
      (x < t ? lo : hi) = u;
      u = (lo + hi) * 0.5F;
    }
    return curve(y1, y2, u);
  }

  float outQuint(float t) { return 1.0F - std::pow(1.0F - std::clamp(t, 0.0F, 1.0F), 5.0F); }
  float outCubic(float t) { return 1.0F - std::pow(1.0F - std::clamp(t, 0.0F, 1.0F), 3.0F); }
  float outBack(float t, float s) {
    t = std::clamp(t, 0.0F, 1.0F) - 1.0F;
    return t * t * ((s + 1.0F) * t + s) + 1.0F;
  }

  void tweenColor(Node& owner, const ColorSpec& from, const ColorSpec& to, int baseMs,
                  std::function<void(const ColorSpec&)> apply) {
    AnimationManager* anims = owner.animationManager();
    if (anims == nullptr) {
      apply(to);
      return;
    }
    anims->cancelForOwner(&owner);
    const Color a = resolveColorSpec(from);
    const Color b = resolveColorSpec(to);
    anims->animate(
        0.0F, 1.0F, static_cast<float>(baseMs), Easing::EaseOutCubic,
        [apply, a, b](float t) { apply(fixedColorSpec(lerpColor(a, b, t))); }, [apply, to]() { apply(to); }, &owner
    );
  }

  void tweenScale(Node& node, float to, int baseMs) {
    AnimationManager* anims = node.animationManager();
    if (anims == nullptr) {
      node.setScale(to);
      return;
    }
    anims->cancelForOwner(&node);
    const float from = node.scale();
    const float overshoot = kusanagi::bounce(2.5F);
    anims->animate(
        0.0F, 1.0F, static_cast<float>(baseMs), Easing::Linear,
        [&node, from, to, overshoot](float t) { node.setScale(from + (to - from) * outBack(t, overshoot)); }, {},
        &node
    );
  }

  void centerIn(Label& label, float x, float y, float w, float h) {
    label.setPosition(std::round(x + (w - label.width()) * 0.5F), std::round(y + (h - label.height()) * 0.5F));
  }

  // Card

  Card::Card(bool hoverable, float hoverAlpha) : m_fill(textA(0.045F)), m_hoverAlpha(hoverAlpha) {
    m_bg = addBox(*this);
    m_bg->setFill(m_fill);
    m_bg->setBorder(textA(0.06F), 1.0F);
    m_bg->setRadius(cardRadius());
    if (hoverable) {
      setCursorShape(kPointer);
      setOnEnter([this](const PointerData&) {
        tweenColor(*m_bg, m_fill, textA(m_hoverAlpha), 160, [this](const ColorSpec& c) { m_bg->setFill(c); });
        m_fill = textA(m_hoverAlpha);
      });
      setOnLeave([this]() {
        tweenColor(*m_bg, m_fill, textA(0.045F), 160, [this](const ColorSpec& c) { m_bg->setFill(c); });
        m_fill = textA(0.045F);
      });
    }
  }

  void Card::setSize(float width, float height) {
    InputArea::setSize(width, height);
    m_bg->setSize(width, height);
  }

  // IconButton

  IconButton::IconButton(char32_t codepoint, float iconPx, bool filled) : m_filled(filled) {
    m_bg = addBox(*this);
    m_icon = static_cast<Label*>(addChild(icon(codepoint, iconPx)));
    setCursorShape(kPointer);
    setOnEnter([this](const PointerData&) { restyle(true); });
    setOnLeave([this]() { restyle(true); });
    setOnClick([this](const PointerData&) {
      if (m_onActivate) m_onActivate();
    });
    pressFeedback(*this, 0.9F);
    restyle(false);
  }

  void IconButton::setIcon(char32_t codepoint) { m_icon->setText(utf8(codepoint)); }

  void IconButton::setFilled(bool filled) {
    if (m_filled == filled) return;
    m_filled = filled;
    restyle(true);
  }

  void IconButton::restyle(bool animate) {
    const ColorSpec bg = m_filled ? accent() : textA(hovered() ? 0.1F : 0.0F);
    const ColorSpec fg = m_filled ? bgPanel() : hovered() ? textA(1.0F) : dim();
    if (animate) {
      tweenColor(*m_bg, m_bgColor, bg, 160, [this](const ColorSpec& c) { m_bg->setFill(c); });
      tweenColor(*m_icon, m_iconColor, fg, 160, [this](const ColorSpec& c) { m_icon->setColor(c); });
    } else {
      m_bg->setFill(bg);
      m_icon->setColor(fg);
    }
    m_bgColor = bg;
    m_iconColor = fg;
  }

  void IconButton::layout(Renderer& renderer) {
    m_bg->setSize(width(), height());
    m_bg->setRadius(width() / 2.0F);
    m_icon->measure(renderer);
    centerIn(*m_icon, 0.0F, 0.0F, width(), height());
  }

  // Tile

  Tile::Tile(std::string look) : m_look(std::move(look)) {
    m_bg = addBox(*this);
    m_icon = static_cast<Label*>(addChild(icon(0, m_look == "icons" ? 22.0F : 19.0F)));
    if (m_look != "icons") {
      m_label = static_cast<Label*>(addChild(text("", 11.0F, true)));
      m_label->setMaxLines(1);
      m_sub = static_cast<Label*>(addChild(text("", 10.0F, false, dim())));
      m_sub->setMaxLines(1);
    }
    setCursorShape(kPointer);
    setOnEnter([this](const PointerData&) { restyle(true); });
    setOnLeave([this]() { restyle(true); });
    setOnClick([this](const PointerData&) {
      if (m_onActivate) m_onActivate();
    });
    pressFeedback(*this, 0.95F);
    restyle(false);
  }

  float Tile::heightFor(const std::string& look) { return look == "icons" ? 58.0F : look == "pills" ? 48.0F : 72.0F; }

  bool Tile::setContent(char32_t codepoint, const std::string& label, const std::string& sub, bool on) {
    bool changed = false;
    if (codepoint != m_codepoint) {
      m_codepoint = codepoint;
      changed = m_icon->setText(utf8(codepoint));
    }
    if (m_label != nullptr) {
      changed = m_label->setText(label) || changed;
      changed = m_sub->setText(sub) || changed;
      m_sub->setVisible(!sub.empty());
    }
    if (on != m_on) {
      m_on = on;
      restyle(true);
    }
    return changed;
  }

  void Tile::restyle(bool animate) {
    const ColorSpec bg = m_on ? accent() : textA(hovered() ? 0.085F : 0.045F);
    const ColorSpec fg = m_on ? bgPanel() : textA(1.0F);
    if (animate) {
      tweenColor(*m_bg, m_bgColor, bg, 180, [this](const ColorSpec& c) { m_bg->setFill(c); });
      tweenColor(*m_icon, m_fgColor, fg, 180, [this](const ColorSpec& c) {
        m_icon->setColor(c);
        if (m_label != nullptr) m_label->setColor(c);
      });
    } else {
      m_bg->setFill(bg);
      m_icon->setColor(fg);
      if (m_label != nullptr) m_label->setColor(fg);
    }
    m_bg->setBorder(m_on ? clearColorSpec() : textA(0.06F), 1.0F);
    if (m_sub != nullptr) m_sub->setColor(m_on ? bgPanel(0.7F) : dim());
    m_bgColor = bg;
    m_fgColor = fg;
  }

  void Tile::layout(Renderer& renderer) {
    const float w = width();
    const float h = height();
    const bool icons = m_look == "icons";
    const bool pills = m_look == "pills";
    m_bg->setSize(w, h);
    m_bg->setRadius(icons || pills ? h / 2.0F : cardRadius());
    m_icon->measure(renderer);
    if (icons) {
      centerIn(*m_icon, 0.0F, 0.0F, w, h);
      return;
    }
    m_icon->setPosition(pills ? 16.0F : 12.0F, pills ? std::round((h - m_icon->height()) / 2.0F) : 11.0F);
    const float left = pills ? 46.0F : 12.0F;
    const float textW = std::max(1.0F, w - left - (pills ? 14.0F : 12.0F));
    m_label->setMaxWidth(textW);
    m_sub->setMaxWidth(textW);
    m_label->measure(renderer);
    m_sub->measure(renderer);
    const float colH = m_label->height() + (m_sub->visible() ? 1.0F + m_sub->height() : 0.0F);
    const float y = pills ? std::round((h - colH) / 2.0F) : h - colH - 10.0F;
    m_label->setPosition(left, y);
    m_sub->setPosition(left, y + m_label->height() + 1.0F);
  }

  // Slider

  Slider::Slider(bool slim) : m_slim(slim) {
    m_track = addBox(*this);
    m_track->setFill(textA(m_slim ? 0.12F : 0.07F));
    m_fill = addBox(*this);
    m_fill->setFill(accent());
    if (m_slim) {
      m_knob = addBox(*this);
      m_knob->setFill(textA(1.0F));
      m_knob->setBorder(accent(), 2.0F);
      m_knob->setRadius(8.0F);
      m_knob->setSize(16.0F, 16.0F);
    }
    m_icon = static_cast<Label*>(addChild(icon(0, 16.0F)));
    if (!m_slim) m_label = static_cast<Label*>(addChild(text("", 11.0F, true)));
    m_valueText = static_cast<Label*>(addChild(text("0%", 11.0F, false, dim())));

    auto iconArea = ui::inputArea({.cursorShape = kPointer, .onClick = [this](const InputArea::PointerData&) {
                                     if (m_onIconClicked) m_onIconClicked();
                                   }});
    m_iconArea = static_cast<InputArea*>(addChild(std::move(iconArea)));

    setCursorShape(kPointer);
    setOnPress([this](const PointerData& d) {
      m_dragging = d.pressed;
      if (m_knob != nullptr) tweenScale(*m_knob, d.pressed ? 1.2F : 1.0F, 120);
      if (d.pressed) moveTo(at(d.localX));
    });
    setOnCancel([this]() { m_dragging = false; });
    setOnMotion([this](const PointerData& d) {
      if (m_dragging && pressed()) moveTo(at(d.localX));
    });
    setOnAxisHandler([this](const PointerData& d) {
      if (d.axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return false;
      const float steps = d.scrollSteps();
      if (steps == 0.0F) return false;
      moveTo(std::clamp(m_value - steps * 0.05F, 0.0F, 1.0F));
      return true;
    });
    restyleTexts();
  }

  float Slider::trackX() const { return m_slim ? 32.0F : 0.0F; }
  float Slider::trackW() const { return std::max(1.0F, m_slim ? width() - 32.0F - 44.0F : width()); }
  float Slider::at(float localX) const { return std::clamp((localX - trackX()) / trackW(), 0.0F, 1.0F); }

  void Slider::moveTo(float v) {
    v = std::clamp(v, 0.0F, 1.0F);
    m_value = v;
    if (animationManager() != nullptr) animationManager()->cancelForOwner(m_fill);
    applyFill(v);
    if (m_onMoved) m_onMoved(v);
  }

  void Slider::setIcon(char32_t codepoint) { m_icon->setText(utf8(codepoint)); }

  bool Slider::setLabel(const std::string& label) { return m_label != nullptr && m_label->setText(label); }

  void Slider::setValue(float value) {
    value = std::clamp(value, 0.0F, 1.0F);
    if (m_dragging || std::fabs(value - m_value) < 1e-4F) return;
    m_value = value;
    AnimationManager* anims = animationManager();
    if (anims == nullptr || width() <= 0.0F) {
      applyFill(value);
      return;
    }
    anims->cancelForOwner(m_fill);
    const float from = m_shown;
    anims->animate(
        0.0F, 1.0F, 200.0F, Easing::EaseOutCubic, [this, from, value](float t) { applyFill(from + (value - from) * t); },
        {}, m_fill
    );
  }

  void Slider::setMuted(bool muted) {
    if (m_muted == muted) return;
    m_muted = muted;
    const ColorSpec from = m_muted ? accent() : textA(0.25F);
    const ColorSpec to = m_muted ? textA(0.25F) : accent();
    tweenColor(*m_track, from, to, 180, [this](const ColorSpec& c) {
      m_fill->setFill(c);
      if (m_knob != nullptr) m_knob->setBorder(c, 2.0F);
    });
    restyleTexts();
  }

  void Slider::restyleTexts() {
    if (m_slim) {
      m_icon->setColor(m_muted ? dim() : textA(1.0F));
      m_valueText->setColor(dim());
      return;
    }
    m_icon->setColor(bgPanel());
    // Dark text over the fill, light text past it.
    const float fillW = std::max(height(), width() * m_shown);
    if (m_label != nullptr) {
      m_label->setColor(fillW > m_label->x() + m_label->width() + 4.0F ? bgPanel() : textA(1.0F));
    }
    m_valueText->setColor(fillW > width() - 14.0F - m_valueText->width() / 2.0F ? bgPanel() : dim());
  }

  void Slider::applyFill(float shown) {
    m_shown = shown;
    const float tw = trackW();
    const float th = m_track->height();
    const float fw = std::max(th, tw * shown);
    m_fill->setSize(fw, th);
    if (m_knob != nullptr) m_knob->setPosition(trackX() + fw - 8.0F, m_track->y() - 5.0F);
    m_valueText->setText(std::to_string(static_cast<int>(std::lround(shown * 100.0F))) + "%");
    restyleTexts();
  }

  void Slider::layout(Renderer& renderer) {
    const float w = width();
    const float h = height();
    const float th = m_slim ? 6.0F : h;
    const float ty = m_slim ? std::round((h - th) / 2.0F) : 0.0F;
    m_track->setPosition(trackX(), ty);
    m_track->setSize(trackW(), th);
    m_track->setRadius(th / 2.0F);
    m_fill->setPosition(trackX(), ty);
    m_fill->setRadius(th / 2.0F);
    const float iconW = m_slim ? 26.0F : th;
    m_icon->measure(renderer);
    centerIn(*m_icon, 0.0F, 0.0F, iconW, h);
    m_iconArea->setPosition(0.0F, 0.0F);
    m_iconArea->setSize(iconW, h);
    if (m_label != nullptr) {
      m_label->measure(renderer);
      m_label->setPosition(th + 2.0F, std::round((h - m_label->height()) / 2.0F));
    }
    m_valueText->setText("100%");
    m_valueText->measure(renderer);
    applyFill(m_shown == 0.0F && m_value != 0.0F ? m_value : m_shown);
    m_valueText->measure(renderer);
    m_valueText->setPosition(
        w - (m_slim ? 2.0F : 14.0F) - m_valueText->width(), std::round((h - m_valueText->height()) / 2.0F)
    );
    restyleTexts();
  }

  // Segmented

  Segmented::Segmented(float fontPx) : m_fontPx(fontPx) {
    m_bg = addBox(*this);
    m_bg->setFill(textA(0.06F));
    m_highlight = addBox(*this);
    m_highlight->setFill(accent());
  }

  int Segmented::indexOf(int value) const {
    for (std::size_t i = 0; i < m_options.size(); ++i) {
      if (m_options[i].value == value) return static_cast<int>(i);
    }
    return 0;
  }

  void Segmented::setOptions(std::vector<Segment> options, int current) {
    for (auto& cell : m_cells) removeChild(cell.area);
    m_cells.clear();
    m_options = std::move(options);
    m_current = current;
    for (std::size_t i = 0; i < m_options.size(); ++i) {
      const int value = m_options[i].value;
      auto area = ui::inputArea({.cursorShape = kPointer, .onClick = [this, value](const InputArea::PointerData&) {
                                   if (m_onPicked) m_onPicked(value);
                                 }});
      Cell cell;
      cell.area = static_cast<InputArea*>(addChild(std::move(area)));
      if (m_options[i].icon != 0) cell.icon = static_cast<Label*>(cell.area->addChild(icon(m_options[i].icon, 14.0F)));
      cell.label = static_cast<Label*>(cell.area->addChild(text(m_options[i].label, m_fontPx)));
      cell.area->setOnEnter([this](const InputArea::PointerData&) { restyle(true); });
      cell.area->setOnLeave([this]() { restyle(true); });
      m_cells.push_back(cell);
    }
    restyle(false);
  }

  void Segmented::setCurrent(int value, bool animate) {
    if (value == m_current) return;
    m_current = value;
    restyle(true);
    const float cell = (width() - 6.0F) / static_cast<float>(std::max<std::size_t>(1, m_options.size()));
    const float to = 3.0F + static_cast<float>(indexOf(value)) * cell;
    AnimationManager* anims = animationManager();
    if (!animate || anims == nullptr) {
      m_highlightX = to;
      m_highlight->setPosition(to, 3.0F);
      return;
    }
    anims->cancelForOwner(m_highlight);
    const float from = m_highlight->x();
    m_sliding = true;
    anims->animate(
        0.0F, 1.0F, 320.0F, Easing::Linear,
        [this, from, to](float t) {
          m_highlightX = from + (to - from) * outQuint(t);
          m_highlight->setPosition(std::round(m_highlightX), 3.0F);
        },
        [this]() { m_sliding = false; }, m_highlight
    );
  }

  void Segmented::restyle(bool animate) {
    const int active = indexOf(m_current);
    for (std::size_t i = 0; i < m_cells.size(); ++i) {
      Cell& cell = m_cells[i];
      const bool on = static_cast<int>(i) == active;
      const ColorSpec c = on ? bgPanel() : cell.area->hovered() ? textA(1.0F) : dim();
      cell.label->setFontWeight(on ? FontWeight::Bold : FontWeight::Normal);
      if (animate) {
        tweenColor(*cell.label, cell.color, c, 200, [&cell](const ColorSpec& col) {
          cell.label->setColor(col);
          if (cell.icon != nullptr) cell.icon->setColor(col);
        });
      } else {
        cell.label->setColor(c);
        if (cell.icon != nullptr) cell.icon->setColor(c);
      }
      cell.color = c;
    }
  }

  void Segmented::layout(Renderer& renderer) {
    const float w = width();
    const float h = height();
    m_bg->setSize(w, h);
    m_bg->setRadius(h / 2.0F);
    const float cell = (w - 6.0F) / static_cast<float>(std::max<std::size_t>(1, m_options.size()));
    m_highlight->setSize(cell, h - 6.0F);
    m_highlight->setRadius((h - 6.0F) / 2.0F);
    // Only this picker's own slide may leave the highlight where it is. Other animations (the panel opening,
    // a page switch) would otherwise leave it on the first cell until a later layout.
    if (!m_sliding || animationManager() == nullptr || !animationManager()->hasActive()) {
      m_sliding = false;
      m_highlightX = 3.0F + static_cast<float>(indexOf(m_current)) * cell;
    }
    m_highlight->setPosition(std::round(m_highlightX), 3.0F);
    for (std::size_t i = 0; i < m_cells.size(); ++i) {
      Cell& c = m_cells[i];
      c.area->setPosition(3.0F + static_cast<float>(i) * cell, 3.0F);
      c.area->setSize(cell, h - 6.0F);
      c.label->measure(renderer);
      float rowW = c.label->width();
      if (c.icon != nullptr) {
        c.icon->measure(renderer);
        rowW += c.icon->width() + 6.0F;
      }
      // Icon and label are top-aligned in a row centred in the cell.
      const float rowH = std::max(c.label->height(), c.icon != nullptr ? c.icon->height() : 0.0F);
      const float rowY = std::round((h - 6.0F - rowH) / 2.0F);
      float x = std::round((cell - rowW) / 2.0F);
      if (c.icon != nullptr) {
        c.icon->setPosition(x, rowY);
        x += c.icon->width() + 6.0F;
      }
      c.label->setPosition(x, rowY);
    }
  }

  // Switch

  Switch::Switch() {
    InputArea::setSize(38.0F, 22.0F);
    m_bg = addBox(*this);
    m_bg->setSize(38.0F, 22.0F);
    m_bg->setRadius(11.0F);
    m_knob = addBox(*this);
    m_knob->setSize(16.0F, 16.0F);
    m_knob->setRadius(8.0F);
    setCursorShape(kPointer);
    setOnClick([this](const PointerData&) {
      if (m_onToggled) m_onToggled(!m_on);
    });
    m_on = true;
    setOn(false, false);
  }

  void Switch::setOn(bool on, bool animate) {
    if (on == m_on) return;
    m_on = on;
    const ColorSpec bg = on ? accent() : textA(0.12F);
    const ColorSpec knob = on ? bgPanel() : textA(1.0F);
    const float to = on ? 38.0F - 16.0F - 3.0F : 3.0F;
    AnimationManager* anims = animationManager();
    if (!animate || anims == nullptr) {
      m_bg->setFill(bg);
      m_knob->setFill(knob);
      m_knob->setPosition(to, 3.0F);
    } else {
      tweenColor(*m_bg, m_bgColor, bg, 200, [this](const ColorSpec& c) { m_bg->setFill(c); });
      tweenColor(*m_knob, m_knobColor, knob, 200, [this](const ColorSpec& c) { m_knob->setFill(c); });
      const float from = m_knob->x();
      const float overshoot = kusanagi::bounce(1.6F);
      anims->animate(
          0.0F, 1.0F, 260.0F, Easing::Linear,
          [this, from, to, overshoot](float t) { m_knob->setPosition(from + (to - from) * outBack(t, overshoot), 3.0F); },
          {}, this
      );
    }
    m_bgColor = bg;
    m_knobColor = knob;
  }

  // Chip

  Chip::Chip(const std::string& label, char32_t codepoint, bool on) : m_on(on) {
    m_bg = addBox(*this);
    if (codepoint != 0) m_icon = static_cast<Label*>(addChild(icon(codepoint, 13.0F, on ? accent() : dim())));
    m_label = static_cast<Label*>(addChild(text(label, 11.0F, false, on ? textA(1.0F) : dim())));
    setCursorShape(kPointer);
    setOnEnter([this](const PointerData&) { restyle(true); });
    setOnLeave([this]() { restyle(true); });
    setOnClick([this](const PointerData&) {
      if (m_onActivate) m_onActivate();
    });
    pressFeedback(*this, 0.94F);
    restyle(false);
  }

  void Chip::setOn(bool on) {
    if (on == m_on) return;
    m_on = on;
    if (m_icon != nullptr) m_icon->setColor(on ? accent() : dim());
    m_label->setColor(on ? textA(1.0F) : dim());
    restyle(true);
  }

  void Chip::restyle(bool animate) {
    const ColorSpec bg = m_on ? accent(hovered() ? 0.32F : 0.22F) : textA(hovered() ? 0.09F : 0.05F);
    if (animate) {
      tweenColor(*m_bg, m_bgColor, bg, 180, [this](const ColorSpec& c) { m_bg->setFill(c); });
    } else {
      m_bg->setFill(bg);
    }
    m_bg->setBorder(m_on ? accent(0.8F) : textA(0.06F), 1.0F);
    m_bgColor = bg;
  }

  void Chip::layout(Renderer& renderer) {
    m_label->measure(renderer);
    float rowW = m_label->width();
    if (m_icon != nullptr) {
      m_icon->measure(renderer);
      rowW += m_icon->width() + 6.0F;
    }
    const float w = rowW + 24.0F;
    const float h = 28.0F;
    InputArea::setSize(w, h);
    m_bg->setSize(w, h);
    m_bg->setRadius(h / 2.0F);
    // Icon and label are top-aligned in a row centred in the chip.
    const float rowH = std::max(m_label->height(), m_icon != nullptr ? m_icon->height() : 0.0F);
    const float rowY = std::round((h - rowH) / 2.0F);
    float x = 12.0F;
    if (m_icon != nullptr) {
      m_icon->setPosition(x, rowY);
      x += m_icon->width() + 6.0F;
    }
    m_label->setPosition(x, rowY);
  }

  // ListRow

  ListRow::ListRow(Look look) : m_look(look) {
    m_bg = addBox(*this);
    m_bg->setFill(textA(0.0F));
    m_icon = static_cast<Label*>(addChild(icon(0, m_look.iconPx, m_look.iconColor)));
    m_text = static_cast<Label*>(addChild(text("", 12.0F)));
    m_text->setMaxLines(1);
    m_trailing = static_cast<Label*>(addChild(icon(0, m_look.trailingPx, m_look.trailingColor)));
    setCursorShape(kPointer);
    setOnEnter([this](const PointerData&) { restyle(true); });
    setOnLeave([this]() { restyle(true); });
    setOnClick([this](const PointerData&) {
      if (m_onActivate) m_onActivate();
    });
    restyle(false);
  }

  bool ListRow::set(char32_t iconCp, const std::string& s, char32_t trailing, bool selected) {
    bool changed = m_icon->setText(utf8(iconCp));
    changed = m_text->setText(s) || changed;
    m_hasTrailing = trailing != 0;
    changed = m_trailing->setText(trailing != 0 ? utf8(trailing) : std::string()) || changed;
    if (selected != m_selected) {
      m_selected = selected;
      m_text->setFontWeight(selected ? FontWeight::Bold : FontWeight::Normal);
      changed = true;
      restyle(true);
    }
    m_trailing->setVisible(m_hasTrailing && (!m_look.trailingOnlySelected || m_selected));
    return changed;
  }

  void ListRow::restyle(bool animate) {
    const ColorSpec bg = m_selected ? accent(0.16F) : textA(hovered() ? 0.06F : 0.0F);
    if (animate) {
      tweenColor(*m_bg, m_bgColor, bg, 160, [this](const ColorSpec& c) { m_bg->setFill(c); });
    } else {
      m_bg->setFill(bg);
    }
    m_bgColor = bg;
    m_icon->setColor(m_selected ? accent() : m_look.iconColor);
  }

  void ListRow::layout(Renderer& renderer) {
    const float w = width();
    const float h = m_look.height;
    InputArea::setSize(w, h);
    m_bg->setSize(w, h);
    m_bg->setRadius(std::max(6.0F, kusanagi::radius() - 8.0F));
    m_icon->measure(renderer);
    m_icon->setPosition(10.0F, std::round((h - m_icon->height()) / 2.0F));
    m_trailing->measure(renderer);
    const float tx = w - 10.0F - (m_hasTrailing ? m_trailing->width() : 0.0F);
    m_trailing->setPosition(tx, std::round((h - m_trailing->height()) / 2.0F));
    const float x = 10.0F + m_icon->width() + 10.0F;
    m_text->setMaxWidth(std::max(1.0F, tx - 8.0F - x));
    m_text->measure(renderer);
    m_text->setPosition(x, std::round((h - m_text->height()) / 2.0F));
  }

  std::unique_ptr<Label> caption(const std::string& s) {
    auto l = text("", 10.0F, true, dim());
    setSpacedText(*l, s, 1.0F);
    return l;
  }

  void capture(std::vector<std::string> command, std::weak_ptr<void> alive, std::function<void(std::string)> done) {
    process::RunCallbacks callbacks;
    auto out = std::make_shared<std::string>();
    callbacks.stdOut = [out](std::string_view chunk) { out->append(chunk); };
    callbacks.onExit = [out, alive, done = std::move(done)](process::RunResult /*result*/) {
      DeferredCall::callLater([out, alive, done]() {
        if (!alive.expired()) done(std::move(*out));
      });
    };
    (void)process::runAsync(command, std::move(callbacks), process::RunOptions{.timeout = std::chrono::seconds(10)});
  }

} // namespace kusanagi::cp
