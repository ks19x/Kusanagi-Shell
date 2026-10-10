// Control kit for the Settings pages (see sp_kit.h). Sizes, radii, colours and timings match the classic
// Kusanagi look.

#include "shell/kusanagi/settings/sp_kit.h"

#include "core/deferred_call.h"
#include "core/timer_manager.h"
#include "cursor-shape-v1-client-protocol.h"
#include "render/animation/animation_manager.h"
#include "render/core/color.h"
#include "render/core/renderer.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <cmath>
#include <wayland-client-protocol.h>

namespace kusanagi::sp {

  namespace {
    constexpr auto kPointer = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    constexpr const char* kIconFont = "JetBrainsMono Nerd Font";

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
    Label* addLabel(Node& parent, std::unique_ptr<Label> l) { return static_cast<Label*>(parent.addChild(std::move(l))); }

    void vcenter(Node& n, float h) { n.setPosition(n.x(), std::round((h - n.height()) / 2.0F)); }

    void pressFeedback(InputArea& area, float pressedScale) {
      area.setOnPress([&area, pressedScale](const InputArea::PointerData& d) {
        tweenScale(area, d.pressed ? pressedScale : 1.0F, 160);
      });
      area.setOnCancel([&area]() { tweenScale(area, 1.0F, 160); });
    }

    void walkSync(Node* n);
  } // namespace

  // Helpers

  std::string utf8Of(char32_t cp) { return utf8(cp); }

  Color resolved(const ColorSpec& c) { return resolveColorSpec(c); }

  Color darker(const Color& c, float factor) {
    // HSV value divided by the factor; hue and saturation are kept.
    const float mx = std::max({c.r, c.g, c.b});
    if (mx <= 0.0F || factor <= 0.0F) return c;
    const float k = (mx / factor) / mx;
    return Color{c.r * k, c.g * k, c.b * k, c.a};
  }

  std::unique_ptr<Label> makeText(const std::string& s, float px, bool bold, const ColorSpec& color,
                                  const std::string& family) {
    return ui::label({
        .text = s,
        .fontSize = px,
        .fontWeight = bold ? FontWeight::Bold : FontWeight::Normal,
        .fontFamily = family.empty() ? kusanagi::font() : family,
        .color = color,
        .baselineMode = LabelBaselineMode::FontLine,
    });
  }

  std::unique_ptr<Label> makeIcon(char32_t cp, float px, const ColorSpec& color) {
    return ui::label({
        .text = cp != 0 ? utf8(cp) : std::string(),
        .fontSize = px,
        .fontFamily = std::string(kIconFont),
        .color = color,
        .baselineMode = LabelBaselineMode::FontLine,
    });
  }

  void setSpacedText(Label& label, const std::string& s, float letterSpacingPx) {
    // Pango letter_spacing is in 1/1024 of the renderer's unit, which is a pixel here.
    const int spacing = static_cast<int>(std::lround(letterSpacingPx * 1024.0F));
    label.setUseMarkup(true);
    label.setText("<span letter_spacing=\"" + std::to_string(spacing) + "\">" + escapeMarkup(s) + "</span>");
  }

  float outCubic(float t) { return 1.0F - std::pow(1.0F - std::clamp(t, 0.0F, 1.0F), 3.0F); }
  float outQuint(float t) { return 1.0F - std::pow(1.0F - std::clamp(t, 0.0F, 1.0F), 5.0F); }
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

  void tweenScale(Node& node, float to, int baseMs, float overshoot) {
    AnimationManager* anims = node.animationManager();
    if (anims == nullptr) {
      node.setScale(to);
      return;
    }
    anims->cancelForOwner(&node);
    const float from = node.scale();
    const float s = kusanagi::bounce(overshoot);
    anims->animate(
        0.0F, 1.0F, static_cast<float>(baseMs), Easing::Linear,
        [&node, from, to, s](float t) { node.setScale(from + (to - from) * outBack(t, s)); }, {}, &node
    );
  }

  // Item

  namespace {
    void walkSync(Node* n) {
      std::vector<Node*> kids;
      kids.reserve(n->children().size());
      for (const auto& c : n->children()) kids.push_back(c.get());
      for (Node* c : kids) {
        if (auto* it = dynamic_cast<Item*>(c)) {
          it->syncTree();
        } else {
          walkSync(c);
        }
      }
    }
  } // namespace

  Item::Item() = default;

  Item* Item::showIf(std::function<bool()> pred) {
    m_showIf = std::move(pred);
    m_shown = !m_showIf || m_showIf();
    setVisible(m_shown);
    return this;
  }

  Item* Item::enabledIf(std::function<bool()> pred) {
    m_enabledIf = std::move(pred);
    m_enabled = !m_enabledIf || m_enabledIf();
    setOpacity(m_enabled ? 1.0F : 0.4F);
    setHitTestVisible(m_enabled);
    return this;
  }

  void Item::syncTree() {
    if (m_enabledIf) {
      const bool e = m_enabledIf();
      if (e != m_enabled) {
        m_enabled = e;
        setOpacity(e ? 1.0F : 0.4F);
        setHitTestVisible(e);
      }
    }
    if (m_showIf) {
      const bool s = m_showIf();
      if (s != m_shown) {
        m_shown = s;
        setVisible(s);
        requestLayout();
      }
    }
    sync();
    walkSync(this);
  }

  Item* Container::addItem(std::unique_ptr<Item> item) { return static_cast<Item*>(addChild(std::move(item))); }

  std::vector<Item*> Container::items() const {
    std::vector<Item*> out;
    for (const auto& c : children()) {
      if (auto* it = dynamic_cast<Item*>(c.get())) out.push_back(it);
    }
    return out;
  }

  float Column::place(Renderer& renderer, float width) {
    width = widthFor(width);
    float y = 0.0F;
    bool first = true;
    for (Item* it : items()) {
      it->setVisible(it->shown());
      if (!it->shown()) continue;
      if (!first) y += m_spacing;
      first = false;
      const float h = it->place(renderer, width);
      it->setPosition(0.0F, std::round(y));
      y += h;
    }
    setSize(width, y);
    return y;
  }

  Repeater::Repeater(std::function<std::vector<std::string>()> keys,
                     std::function<std::unique_ptr<Item>(const std::string& key)> make, float spacing)
      : Column(spacing), m_keysFn(std::move(keys)), m_make(std::move(make)) {
    showIf([this]() { return m_keysFn && !m_keysFn().empty(); });
    sync();
  }

  void Repeater::sync() {
    std::vector<std::string> keys = m_keysFn ? m_keysFn() : std::vector<std::string>{};
    if (m_built && keys == m_keys) return;
    m_built = true;
    for (Item* it : items()) (void)removeChild(it);
    m_keys = std::move(keys);
    for (const auto& k : m_keys) {
      if (auto item = m_make(k)) addItem(std::move(item));
    }
    requestLayout();
  }

  float Flow::place(Renderer& renderer, float width) {
    width = widthFor(width);
    float x = 0.0F;
    float y = 0.0F;
    float lineH = 0.0F;
    for (Item* it : items()) {
      it->setVisible(it->shown());
      if (!it->shown()) continue;
      const float h = it->place(renderer, width);
      const float w = it->width();
      if (x > 0.0F && x + w > width + 0.5F) {
        y += lineH + m_spacing;
        x = 0.0F;
        lineH = 0.0F;
      }
      it->setPosition(std::round(x), std::round(y));
      x += w + m_spacing;
      lineH = std::max(lineH, h);
    }
    const float total = y + lineH;
    setSize(width, total);
    return total;
  }

  float HRow::place(Renderer& renderer, float width) {
    float x = 0.0F;
    float h = 0.0F;
    bool first = true;
    std::vector<Item*> shown;
    for (Item* it : items()) {
      it->setVisible(it->shown());
      if (!it->shown()) continue;
      if (!first) x += m_spacing;
      first = false;
      h = std::max(h, it->place(renderer, width));
      it->setPosition(std::round(x), 0.0F);
      x += it->width();
      shown.push_back(it);
    }
    for (Item* it : shown) vcenter(*it, h);
    setSize(fixedWidth() > 0.0F ? fixedWidth() : x, h);
    return h;
  }

  Poll::Poll(int ms, std::function<void()> fn) : m_timer(std::make_unique<Timer>()), m_fn(std::move(fn)) {
    showIf([]() { return false; }); // Takes no room (and no spacing) in its column.
    if (m_fn) {
      DeferredCall::callLater([alive = std::weak_ptr<int>(m_alive), fnp = &m_fn]() {
        if (!alive.expired() && *fnp) (*fnp)();
      });
    }
    m_timer->startRepeating(std::chrono::milliseconds(ms), [this]() {
      if (m_fn) m_fn();
    });
  }

  Poll::~Poll() = default;

  float Poll::place(Renderer& /*renderer*/, float /*width*/) {
    setSize(0.0F, 0.0F);
    return 0.0F;
  }

  float Gap::place(Renderer& /*renderer*/, float width) {
    setSize(widthFor(width), m_h);
    return m_h;
  }

  // Text

  Text::Text(std::string text, TextOpts opts) : m_opts(std::move(opts)), m_text(std::move(text)) {
    m_label = addLabel(*this, makeText("", m_opts.px, m_opts.bold, m_opts.color, m_opts.family));
    if (m_opts.wrap) {
      m_label->setMaxLines(0);
    } else {
      m_label->setMaxLines(1);
      if (m_opts.elide) m_label->setEllipsize(TextEllipsize::End);
    }
    setText(m_text);
  }

  Text* Text::bindText(std::function<std::string()> fn) {
    m_bind = std::move(fn);
    setText(m_bind());
    return this;
  }

  Text* Text::bindColor(std::function<ColorSpec()> fn) {
    m_bindColor = std::move(fn);
    m_label->setColor(m_bindColor());
    return this;
  }

  void Text::setText(const std::string& s) {
    m_text = s;
    if (m_opts.letterSpacing > 0.0F) {
      setSpacedText(*m_label, s, m_opts.letterSpacing);
    } else if (m_label->setText(s)) {
      requestLayout();
    }
  }

  void Text::sync() {
    if (m_bind) {
      const std::string s = m_bind();
      if (s != m_text) {
        setText(s);
        requestLayout();
      }
    }
    if (m_bindColor) m_label->setColor(m_bindColor());
  }

  float Text::place(Renderer& renderer, float width) {
    width = widthFor(width);
    m_label->setMaxWidth(m_opts.wrap || m_opts.elide ? std::max(1.0F, width) : 0.0F);
    m_label->measure(renderer);
    m_label->setPosition(0.0F, m_opts.topPadding);
    const float h = m_label->height() + m_opts.topPadding;
    setSize(m_opts.wrap || (m_opts.elide && fixedWidth() > 0.0F) ? width : m_label->width(), h);
    return h;
  }

  Heading::Heading(std::string text, float topPadding)
      : Text(std::move(text), TextOpts{.px = 11.0F, .bold = true, .color = dim(), .topPadding = topPadding}) {}

  // Group

  Group::Group(std::string title, std::string hint, char32_t icon) : m_title(std::move(title)), m_hint(std::move(hint)) {
    if (!m_title.empty()) {
      if (icon != 0) {
        m_iconTile = addBox(*this);
        m_iconTile->setFill(accent(0.14F));
        m_iconTile->setRadius(9.0F);
        m_iconTile->setSize(28.0F, 28.0F);
        m_icon = addLabel(*this, makeIcon(icon, 15.0F, accent()));
      }
      m_titleLabel = addLabel(*this, makeText(m_title, 14.0F, true));
      m_titleLabel->setMaxLines(1);
      if (!m_hint.empty()) {
        m_hintLabel = addLabel(*this, makeText(m_hint, 11.0F, false, dim()));
        m_hintLabel->setMaxLines(0);
      }
    }
    m_card = addBox(*this);
    m_body = static_cast<Column*>(Node::addChild(std::make_unique<Column>(14.0F)));
  }

  Item* Group::addItem(std::unique_ptr<Item> item) { return m_body->addItem(std::move(item)); }

  Group* Group::bindHint(std::function<std::string()> fn) {
    m_hintFn = std::move(fn);
    if (m_hintLabel == nullptr && !m_title.empty()) {
      m_hintLabel = addLabel(*this, makeText("", 11.0F, false, dim()));
      m_hintLabel->setMaxLines(0);
    }
    sync();
    return this;
  }

  Group* Group::bindTitle(std::function<std::string()> fn) {
    m_titleFn = std::move(fn);
    sync();
    return this;
  }

  void Group::sync() {
    if (m_titleFn && m_titleLabel != nullptr) {
      m_title = m_titleFn();
      if (m_titleLabel->setText(m_title)) requestLayout();
    }
    if (!m_hintFn || m_hintLabel == nullptr) return;
    m_hint = m_hintFn();
    m_hintLabel->setVisible(!m_hint.empty());
    if (m_hintLabel->setText(m_hint)) requestLayout();
  }

  float Group::place(Renderer& renderer, float width) {
    width = widthFor(width);
    float y = 0.0F;
    if (m_titleLabel != nullptr) {
      const float textX = m_iconTile != nullptr ? 38.0F : 0.0F;
      m_titleLabel->measure(renderer);
      float colH = m_titleLabel->height();
      if (m_hintLabel != nullptr && !m_hint.empty()) {
        m_hintLabel->setMaxWidth(std::max(1.0F, width - textX));
        m_hintLabel->measure(renderer);
        colH += 2.0F + m_hintLabel->height();
      }
      const float rowH = std::max(m_iconTile != nullptr ? 28.0F : 0.0F, colH);
      if (m_iconTile != nullptr) {
        const float ty = std::round((rowH - 28.0F) / 2.0F);
        m_iconTile->setPosition(0.0F, ty);
        m_icon->measure(renderer);
        m_icon->setPosition(std::round((28.0F - m_icon->width()) / 2.0F), ty + std::round((28.0F - m_icon->height()) / 2.0F));
      }
      const float cy = std::round((rowH - colH) / 2.0F);
      m_titleLabel->setPosition(textX, cy);
      if (m_hintLabel != nullptr && !m_hint.empty()) m_hintLabel->setPosition(textX, cy + m_titleLabel->height() + 2.0F);
      y = rowH + 10.0F;
    }
    const float bodyH = m_body->place(renderer, std::max(1.0F, width - 36.0F));
    m_body->setPosition(18.0F, y + 16.0F);
    m_card->setPosition(0.0F, y);
    m_card->setSize(width, bodyH + 32.0F);
    m_card->setRadius(std::max(10.0F, kusanagi::radius() - 2.0F));
    m_card->setFill(textA(0.035F));
    m_card->setBorder(textA(0.045F), kusanagi::surfaceBorderWidth() > 0.0F ? 1.0F : 0.0F);
    const float h = y + bodyH + 32.0F;
    setSize(width, h);
    return h;
  }

  // Fold

  Fold::Fold(std::string title, std::string hint, bool open) : m_open(open) {
    auto header = ui::inputArea({.cursorShape = kPointer});
    m_header = static_cast<InputArea*>(addChild(std::move(header)));
    m_headerBg = addBox(*m_header);
    m_headerBg->setRadius(9.0F);
    m_headerBg->setFill(textA(0.0F));
    m_chevron = addLabel(*m_header, makeIcon(0xf0142, 16.0F, open ? accent() : dim()));
    m_titleLabel = addLabel(*m_header, makeText(title, 12.0F, true));
    m_titleLabel->setMaxLines(1);
    m_hintLabel = addLabel(*m_header, makeText(hint, 10.0F, false, dim()));
    m_hintLabel->setMaxLines(1);
    m_hintLabel->setEllipsize(TextEllipsize::End);
    m_hintLabel->setTextAlign(TextAlign::End);
    m_header->setOnEnter([this](const InputArea::PointerData&) {
      tweenColor(*m_headerBg, textA(0.0F), textA(0.05F), 120, [this](const ColorSpec& c) { m_headerBg->setFill(c); });
    });
    m_header->setOnLeave([this]() {
      tweenColor(*m_headerBg, textA(0.05F), textA(0.0F), 120, [this](const ColorSpec& c) { m_headerBg->setFill(c); });
    });
    m_header->setOnClick([this](const InputArea::PointerData&) { setOpen(!m_open); });
    m_clip = addChild(ui::node({}));
    m_clip->setClipChildren(true);
    m_body = static_cast<Column*>(m_clip->addChild(std::make_unique<Column>(6.0F)));
    m_body->setOpacity(open ? 1.0F : 0.0F);
    m_chevron->setRotation(open ? static_cast<float>(M_PI) / 2.0F : 0.0F);
    m_shownH = -1.0F; // The first place() snaps instead of animating.
  }

  Item* Fold::addItem(std::unique_ptr<Item> item) { return m_body->addItem(std::move(item)); }

  Fold* Fold::bindHint(std::function<std::string()> fn) {
    m_hintFn = std::move(fn);
    m_hintLabel->setText(m_hintFn());
    return this;
  }

  void Fold::sync() {
    if (m_hintFn && m_hintLabel->setText(m_hintFn())) requestLayout();
  }

  void Fold::setOpen(bool open) {
    if (open == m_open) return;
    m_open = open;
    m_chevron->setColor(open ? accent() : dim());
    AnimationManager* anims = animationManager();
    const float target = open ? m_bodyH + 10.0F : 0.0F;
    if (anims == nullptr) {
      m_shownH = target;
      m_body->setOpacity(open ? 1.0F : 0.0F);
      requestLayout();
      return;
    }
    anims->cancelForOwner(m_clip);
    anims->cancelForOwner(m_body);
    anims->cancelForOwner(m_chevron);
    const float fromH = std::max(0.0F, m_shownH);
    const float fromRot = m_chevron->rotation();
    const float toRot = open ? static_cast<float>(M_PI) / 2.0F : 0.0F;
    m_animating = true;
    anims->animate(
        0.0F, 1.0F, 240.0F, Easing::Linear,
        [this, fromH](float t) {
          const float to = m_open ? m_bodyH + 10.0F : 0.0F;
          m_shownH = fromH + (to - fromH) * outCubic(t);
          sp::requestLayout();
        },
        [this]() {
          m_animating = false;
          sp::requestLayout();
        },
        m_clip
    );
    const float fromOp = m_body->opacity();
    anims->animate(
        0.0F, 1.0F, 180.0F, Easing::Linear,
        [this, fromOp, open](float t) { m_body->setOpacity(fromOp + ((open ? 1.0F : 0.0F) - fromOp) * t); }, {}, m_body
    );
    anims->animate(
        0.0F, 1.0F, 200.0F, Easing::Linear,
        [this, fromRot, toRot](float t) { m_chevron->setRotation(fromRot + (toRot - fromRot) * outCubic(t)); }, {},
        m_chevron
    );
  }

  float Fold::place(Renderer& renderer, float width) {
    width = widthFor(width);
    m_header->setSize(width, 38.0F);
    m_headerBg->setSize(width, 38.0F);
    m_chevron->measure(renderer);
    m_chevron->setPosition(8.0F, std::round((38.0F - m_chevron->height()) / 2.0F));
    m_chevron->setTransformOrigin(m_chevron->width() / 2.0F, m_chevron->height() / 2.0F);
    m_titleLabel->measure(renderer);
    m_titleLabel->setPosition(32.0F, std::round((38.0F - m_titleLabel->height()) / 2.0F));
    const float hintW = width * 0.55F;
    m_hintLabel->setMaxWidth(hintW);
    m_hintLabel->measure(renderer);
    m_hintLabel->setPosition(width - 10.0F - m_hintLabel->width(), std::round((38.0F - m_hintLabel->height()) / 2.0F));

    m_bodyH = m_body->place(renderer, std::max(1.0F, width - 8.0F));
    m_body->setPosition(8.0F, 4.0F);
    if (!m_animating || m_shownH < 0.0F) m_shownH = m_open ? m_bodyH + 10.0F : 0.0F;
    m_clip->setPosition(0.0F, 38.0F);
    m_clip->setSize(width, std::max(0.0F, m_shownH));
    m_clip->setVisible(m_shownH > 0.5F);
    const float h = 38.0F + std::max(0.0F, m_shownH);
    setSize(width, h);
    return h;
  }

  // Row

  Row::Row(std::string label, std::string hint, std::unique_ptr<Item> control) {
    m_label = addLabel(*this, makeText(label, 12.0F));
    m_label->setMaxLines(1);
    if (!hint.empty()) {
      m_hint = addLabel(*this, makeText(hint, 10.0F, false, dim()));
      m_hint->setMaxLines(0);
    }
    m_control = static_cast<Item*>(addChild(std::move(control)));
  }

  Row* Row::bindHint(std::function<std::string()> fn) {
    m_hintFn = std::move(fn);
    if (m_hint == nullptr) {
      m_hint = addLabel(*this, makeText("", 10.0F, false, dim()));
      m_hint->setMaxLines(0);
    }
    sync();
    return this;
  }

  void Row::sync() {
    if (!m_hintFn || m_hint == nullptr) return;
    const std::string h = m_hintFn();
    m_hint->setVisible(!h.empty());
    if (m_hint->setText(h)) requestLayout();
  }

  float Row::place(Renderer& renderer, float width) {
    width = widthFor(width);
    const float ch = m_control->place(renderer, width);
    const float cw = m_control->width();
    const float colW = std::max(1.0F, width - cw - 16.0F);
    m_label->measure(renderer);
    float colH = m_label->height();
    if (m_hint != nullptr && m_hint->visible()) {
      m_hint->setMaxWidth(colW);
      m_hint->measure(renderer);
      colH += m_hint->height();
    }
    // The row is as tall as the control (at least 34). A tall hint overflows rather than growing the row.
    const float h = std::max(34.0F, ch);
    const float cy = std::round((h - colH) / 2.0F);
    m_label->setPosition(0.0F, cy);
    if (m_hint != nullptr) m_hint->setPosition(0.0F, cy + m_label->height());
    m_control->setPosition(width - cw, std::round((h - ch) / 2.0F));
    setSize(width, h);
    return h;
  }

  // Switch

  Switch::Switch(Binding binding) : m_binding(std::move(binding)) {
    m_bg = addBox(*this);
    m_bg->setSize(38.0F, 22.0F);
    m_bg->setRadius(11.0F);
    m_knob = addBox(*this);
    m_knob->setSize(16.0F, 16.0F);
    m_knob->setRadius(8.0F);
    setCursorShape(kPointer);
    setOnClick([this](const PointerData&) {
      const bool next = !m_on;
      apply(next, true);
      m_binding.set(next);
    });
    apply(truthy(m_binding.get()), false);
  }

  void Switch::apply(bool on, bool animate) {
    if (on == m_on && !m_first) return;
    m_first = false;
    m_on = on;
    const ColorSpec bg = on ? accent() : textA(0.12F);
    const ColorSpec knob = on ? bgPanel() : textA(1.0F);
    const float to = on ? (fixedWidth() > 0.0F ? fixedWidth() : 38.0F) - 16.0F - 3.0F : 3.0F;
    AnimationManager* anims = animationManager();
    if (!animate || anims == nullptr) {
      m_bg->setFill(bg);
      m_knob->setFill(knob);
      m_knob->setPosition(to, 3.0F);
    } else {
      tweenColor(*m_bg, m_bgColor, bg, 200, [this](const ColorSpec& c) { m_bg->setFill(c); });
      tweenColor(*m_knob, m_knobColor, knob, 200, [this](const ColorSpec& c) { m_knob->setFill(c); });
      anims->cancelForOwner(this);
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

  void Switch::sync() { apply(truthy(m_binding.get()), true); }

  float Switch::place(Renderer& /*renderer*/, float /*width*/) {
    // A fixed width stretches the track (the bar editor's fields do this).
    const float w = fixedWidth() > 0.0F ? fixedWidth() : 38.0F;
    if (w != m_bg->width()) {
      m_bg->setSize(w, 22.0F);
      if (AnimationManager* anims = animationManager(); anims != nullptr) anims->cancelForOwner(this); // Stop the knob's slide.
      m_knob->setPosition(m_on ? w - 16.0F - 3.0F : 3.0F, 3.0F);
    }
    setSize(w, 22.0F);
    return 22.0F;
  }

  // Slider

  Slider::Slider(Binding binding, SliderOpts opts) : m_binding(std::move(binding)), m_opts(std::move(opts)) {
    m_track = addBox(*this);
    m_track->setFill(textA(0.07F));
    m_fill = addBox(*this);
    m_fill->setFill(accent());
    m_icon = addLabel(*this, makeIcon(m_opts.icon, 16.0F, bgPanel()));
    m_label = addLabel(*this, makeText(m_opts.label, 11.0F, true));
    m_label->setMaxLines(1);
    m_valueText = addLabel(*this, makeText("", 11.0F, false, dim()));
    m_valueText->setMaxLines(1);
    if (m_opts.onIconClicked) {
      // The icon's area sits above the drag area, so a click on it doesn't move the slider.
      m_iconArea = static_cast<InputArea*>(addChild(ui::inputArea({.cursorShape = kPointer, .onClick = [this](const InputArea::PointerData&) {
                                                                    if (m_opts.onIconClicked) m_opts.onIconClicked();
                                                                  }})));
    }
    if (m_opts.iconFn) m_icon->setText(utf8(m_opts.iconFn()));
    if (m_opts.labelFn) m_label->setText(m_opts.labelFn());
    if (m_opts.muted) {
      m_muted = m_opts.muted();
      m_fill->setFill(m_muted ? textA(0.25F) : accent());
    }
    setCursorShape(kPointer);
    setOnPress([this](const PointerData& d) {
      m_dragging = d.pressed;
      if (d.pressed) moveTo(at(d.localX));
    });
    setOnCancel([this]() { m_dragging = false; });
    setOnMotion([this](const PointerData& d) {
      if (m_dragging && pressed()) moveTo(at(d.localX));
    });
    setOnAxisHandler([this](const PointerData& d) {
      if (d.axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return false;
      const float steps = d.scrollSteps();
      if (steps == 0.0F) return true;
      moveTo(std::clamp(m_value - steps * m_opts.step, 0.0F, 1.0F));
      return true;
    });
    m_value = std::clamp(m_opts.toUnit ? m_opts.toUnit(m_binding.get()) : 0.0F, 0.0F, 1.0F);
    m_valueText->setText(m_opts.text ? m_opts.text(m_binding.get()) : "");
  }

  float Slider::at(float localX) const { return std::clamp(localX / std::max(1.0F, width()), 0.0F, 1.0F); }

  void Slider::moveTo(float unit) {
    unit = std::clamp(unit, 0.0F, 1.0F);
    if (AnimationManager* a = animationManager(); a != nullptr) a->cancelForOwner(m_fill);
    applyFill(unit);
    if (m_opts.fromUnit) m_binding.set(m_opts.fromUnit(unit));
    m_value = std::clamp(m_opts.toUnit ? m_opts.toUnit(m_binding.get()) : unit, 0.0F, 1.0F);
    if (m_opts.text && m_valueText->setText(m_opts.text(m_binding.get()))) requestLayout();
  }

  void Slider::sync() {
    const json v = m_binding.get();
    if (m_opts.text && m_valueText->setText(m_opts.text(v))) requestLayout();
    if (m_opts.iconFn && m_icon->setText(utf8(m_opts.iconFn()))) requestLayout();
    if (m_opts.labelFn && m_label->setText(m_opts.labelFn())) requestLayout();
    if (m_opts.muted) {
      const bool muted = m_opts.muted();
      if (muted != m_muted) {
        tweenColor(*m_track, m_muted ? textA(0.25F) : accent(), muted ? textA(0.25F) : accent(), 180,
                   [this](const ColorSpec& c) { m_fill->setFill(c); });
        m_muted = muted;
      }
    }
    const float value = std::clamp(m_opts.toUnit ? m_opts.toUnit(v) : 0.0F, 0.0F, 1.0F);
    if (m_dragging) return; // Keep showing the drag position while pressed.
    if (std::fabs(value - m_shown) < 1e-4F) {
      m_value = value;
      return;
    }
    m_value = value;
    AnimationManager* anims = animationManager();
    if (anims == nullptr || width() <= 0.0F || m_shown < 0.0F) {
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

  void Slider::applyFill(float shown) {
    m_shown = shown;
    const float h = height();
    m_fill->setSize(std::max(h, width() * shown), h);
    restyleTexts();
  }

  void Slider::restyleTexts() {
    // Dark text over the fill, light text past it.
    const float fillW = m_fill->width();
    m_label->setColor(fillW > m_label->x() + m_label->width() + 4.0F ? bgPanel() : textA(1.0F));
    m_valueText->setColor(fillW > width() - 14.0F - m_valueText->width() / 2.0F ? bgPanel() : dim());
  }

  float Slider::place(Renderer& renderer, float width) {
    width = widthFor(width);
    const float h = m_opts.height;
    setSize(width, h);
    m_track->setSize(width, h);
    m_track->setRadius(h / 2.0F);
    m_fill->setRadius(h / 2.0F);
    m_icon->measure(renderer);
    m_icon->setPosition(std::round((h - m_icon->width()) / 2.0F), std::round((h - m_icon->height()) / 2.0F));
    if (m_iconArea != nullptr) {
      m_iconArea->setPosition(0.0F, 0.0F);
      m_iconArea->setSize(h, h);
    }
    m_label->measure(renderer);
    m_label->setPosition(h + 2.0F, std::round((h - m_label->height()) / 2.0F));
    m_valueText->measure(renderer);
    m_valueText->setPosition(width - 14.0F - m_valueText->width(), std::round((h - m_valueText->height()) / 2.0F));
    applyFill(m_shown < 0.0F ? m_value : m_shown);
    return h;
  }

  // Segmented

  Segmented::Segmented(Binding binding, std::vector<Option> options, float width, float fontPx)
      : m_binding(std::move(binding)), m_options(std::move(options)), m_fontPx(fontPx) {
    setFixedWidth(width);
    m_bg = addBox(*this);
    m_bg->setFill(textA(0.06F));
    m_highlight = addBox(*this);
    m_highlight->setFill(accent());
    for (std::size_t i = 0; i < m_options.size(); ++i) {
      const json value = m_options[i].value;
      auto area = ui::inputArea({.cursorShape = kPointer, .onClick = [this, value](const InputArea::PointerData&) {
                                   m_binding.set(value);
                                   sync();
                                 }});
      Cell cell;
      cell.area = static_cast<InputArea*>(addChild(std::move(area)));
      if (m_options[i].icon != 0) cell.icon = addLabel(*cell.area, makeIcon(m_options[i].icon, 14.0F));
      cell.label = addLabel(*cell.area, makeText(m_options[i].label, m_fontPx));
      cell.label->setMaxLines(1);
      cell.area->setOnEnter([this](const InputArea::PointerData&) { restyle(true); });
      cell.area->setOnLeave([this]() { restyle(true); });
      m_cells.push_back(cell);
    }
    m_index = indexOf(m_binding.get());
    restyle(false);
  }

  int Segmented::indexOf(const json& v) const {
    for (std::size_t i = 0; i < m_options.size(); ++i) {
      if (m_options[i].value == v) return static_cast<int>(i);
    }
    return 0; // Unknown values highlight the first option.
  }

  void Segmented::restyle(bool animate) {
    for (std::size_t i = 0; i < m_cells.size(); ++i) {
      Cell& cell = m_cells[i];
      const bool on = static_cast<int>(i) == m_index;
      const ColorSpec c = on ? bgPanel() : cell.area->hovered() ? textA(1.0F) : dim();
      if (cell.label->fontWeight() != (on ? FontWeight::Bold : FontWeight::Normal)) {
        cell.label->setFontWeight(on ? FontWeight::Bold : FontWeight::Normal);
        requestLayout();
      }
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

  void Segmented::sync() {
    const int idx = indexOf(m_binding.get());
    if (idx == m_index) return;
    m_index = idx;
    restyle(true);
    const float cell = (width() - 6.0F) / static_cast<float>(std::max<std::size_t>(1, m_options.size()));
    const float to = 3.0F + static_cast<float>(idx) * cell;
    AnimationManager* anims = animationManager();
    if (anims == nullptr || width() <= 0.0F) {
      m_highlightX = to;
      return;
    }
    anims->cancelForOwner(m_highlight);
    const float from = m_highlight->x();
    anims->animate(
        0.0F, 1.0F, 320.0F, Easing::Linear,
        [this, from, to](float t) {
          m_highlightX = from + (to - from) * outQuint(t);
          m_highlight->setPosition(std::round(m_highlightX), 3.0F);
        },
        {}, m_highlight
    );
  }

  float Segmented::place(Renderer& renderer, float width) {
    const float w = widthFor(width);
    const float h = 32.0F;
    setSize(w, h);
    m_bg->setSize(w, h);
    m_bg->setRadius(h / 2.0F);
    const float cell = (w - 6.0F) / static_cast<float>(std::max<std::size_t>(1, m_options.size()));
    m_highlight->setSize(cell, h - 6.0F);
    m_highlight->setRadius((h - 6.0F) / 2.0F);
    AnimationManager* anims = animationManager();
    if (m_first || anims == nullptr || !anims->hasActive()) {
      m_highlightX = 3.0F + static_cast<float>(m_index) * cell;
      m_first = false;
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
      // The row is centred in the cell but its items are top-aligned (the classic look).
      const float rowH = std::max(c.label->height(), c.icon != nullptr ? c.icon->height() : 0.0F);
      const float rowY = std::round((h - 6.0F - rowH) / 2.0F);
      float x = std::round((cell - rowW) / 2.0F);
      if (c.icon != nullptr) {
        c.icon->setPosition(x, rowY);
        x += c.icon->width() + 6.0F;
      }
      c.label->setPosition(x, rowY);
    }
    return h;
  }

  // IconButton

  IconButton::IconButton(char32_t icon, std::function<void()> onClick, float size, float iconPx)
      : m_size(size), m_onClick(std::move(onClick)) {
    m_bg = addBox(*this);
    m_icon = addLabel(*this, makeIcon(icon, iconPx));
    setCursorShape(kPointer);
    setOnEnter([this](const PointerData&) { restyle(true); });
    setOnLeave([this]() { restyle(true); });
    setOnClick([this](const PointerData&) {
      if (m_enabled && m_onClick) m_onClick();
    });
    pressFeedback(*this, 0.9F);
    restyle(false);
  }

  IconButton* IconButton::setFilled(bool filled) {
    m_filled = filled;
    restyle(false);
    return this;
  }

  IconButton* IconButton::enabledWhen(std::function<bool()> fn) {
    m_enabledFn = std::move(fn);
    sync();
    return this;
  }

  void IconButton::sync() {
    if (!m_enabledFn) return;
    m_enabled = m_enabledFn();
    setOpacity(m_enabled ? 1.0F : 0.3F);
    setEnabled(m_enabled);
  }

  void IconButton::restyle(bool animate) {
    const bool hov = hovered() && m_enabled;
    const ColorSpec bg = m_filled ? accent() : textA(hov ? 0.1F : 0.0F);
    const ColorSpec fg = m_filled ? bgPanel() : hov ? textA(1.0F) : dim();
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

  float IconButton::place(Renderer& renderer, float /*width*/) {
    setSize(m_size, m_size);
    m_bg->setSize(m_size, m_size);
    m_bg->setRadius(m_size / 2.0F);
    m_icon->measure(renderer);
    m_icon->setPosition(std::round((m_size - m_icon->width()) / 2.0F), std::round((m_size - m_icon->height()) / 2.0F));
    return m_size;
  }

  // Stepper

  Stepper::Stepper(Binding binding, StepperOpts opts) : m_binding(std::move(binding)), m_opts(std::move(opts)) {
    m_minus = static_cast<IconButton*>(addChild(std::make_unique<IconButton>(0xf0374, [this]() {
      m_binding.set(json(static_cast<double>(std::max(m_opts.from, current() - m_opts.step)) * m_opts.scale));
      sync();
    }, 28.0F)));
    m_text = addLabel(*this, makeText("", 13.0F, true));
    m_text->setMaxLines(1);
    m_plus = static_cast<IconButton*>(addChild(std::make_unique<IconButton>(0xf0415, [this]() {
      m_binding.set(json(static_cast<double>(std::min(m_opts.to, current() + m_opts.step)) * m_opts.scale));
      sync();
    }, 28.0F)));
    sync();
  }

  int Stepper::current() const {
    const json v = m_binding.get();
    const double d = v.is_number() ? v.get<double>() : 0.0;
    return static_cast<int>(std::lround(d / m_opts.scale));
  }

  void Stepper::sync() {
    if (m_text->setText(std::to_string(current()) + m_opts.suffix)) requestLayout();
  }

  float Stepper::place(Renderer& renderer, float width) {
    m_minus->place(renderer, width);
    m_plus->place(renderer, width);
    m_text->measure(renderer);
    const float tw = std::max(34.0F, m_text->width());
    m_minus->setPosition(0.0F, 0.0F);
    m_text->setPosition(std::round(28.0F + 6.0F + (tw - m_text->width()) / 2.0F), std::round((28.0F - m_text->height()) / 2.0F));
    m_plus->setPosition(28.0F + 6.0F + tw + 6.0F, 0.0F);
    setSize(28.0F + 6.0F + tw + 6.0F + 28.0F, 28.0F);
    return 28.0F;
  }

  // Chip

  Chip::Chip(std::string label, char32_t icon, std::string fontFamily) : m_text(std::move(label)) {
    m_bg = addBox(*this);
    if (icon != 0) m_icon = addLabel(*this, makeIcon(icon, 13.0F, dim()));
    m_label = addLabel(*this, makeText(m_text, 11.0F, false, dim(), fontFamily));
    m_label->setMaxLines(1);
    setCursorShape(kPointer);
    setOnEnter([this](const PointerData&) { restyle(true); });
    setOnLeave([this]() { restyle(true); });
    setOnClick([this](const PointerData&) {
      if (m_onClick) m_onClick();
    });
    pressFeedback(*this, 0.94F);
    restyle(false);
  }

  Chip* Chip::onWhen(std::function<bool()> fn) {
    m_onWhen = std::move(fn);
    m_on = m_onWhen();
    if (m_icon != nullptr) m_icon->setColor(m_on ? accent() : dim());
    m_label->setColor(m_on ? textA(1.0F) : dim());
    restyle(false);
    return this;
  }

  Chip* Chip::labelFrom(std::function<std::string()> fn) {
    m_labelFn = std::move(fn);
    m_label->setText(m_labelFn());
    return this;
  }

  void Chip::sync() {
    if (m_labelFn && m_label->setText(m_labelFn())) requestLayout();
    if (!m_onWhen) return;
    const bool on = m_onWhen();
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

  float Chip::place(Renderer& renderer, float /*width*/) {
    m_label->measure(renderer);
    float rowW = m_label->width();
    if (m_icon != nullptr) {
      m_icon->measure(renderer);
      rowW += m_icon->width() + 6.0F;
    }
    const float w = std::round(rowW + 24.0F);
    const float h = 28.0F;
    setSize(w, h);
    m_bg->setSize(w, h);
    m_bg->setRadius(h / 2.0F);
    // The row is centred in the chip but its items are top-aligned (the classic look).
    const float rowH = std::max(m_label->height(), m_icon != nullptr ? m_icon->height() : 0.0F);
    const float rowY = std::round((h - rowH) / 2.0F);
    float x = std::round((w - rowW) / 2.0F);
    if (m_icon != nullptr) {
      m_icon->setPosition(x, rowY);
      x += m_icon->width() + 6.0F;
    }
    m_label->setPosition(x, rowY);
    return h;
  }

  std::unique_ptr<Chip> choiceChip(const std::string& label, const std::string& path, json value, std::string fontFamily) {
    auto chip = std::make_unique<Chip>(label, 0, std::move(fontFamily));
    chip->onWhen([path, value]() { return sp::value(path) == value; })->onClick([path, value]() { set(path, value); });
    return chip;
  }

  // Field

  Field::Field(std::optional<Binding> binding, FieldOpts opts) : m_binding(std::move(binding)), m_opts(std::move(opts)) {
    setFixedWidth(m_opts.width);
    m_bg = addBox(*this);
    m_bg->setFill(textA(0.06F));
    if (m_opts.icon != 0) m_icon = addLabel(*this, makeIcon(m_opts.icon, 14.0F, dim()));
    m_placeholder = addLabel(*this, makeText(m_opts.placeholder, 12.0F, false, dim()));
    m_placeholder->setMaxLines(1);
    addChild(ui::input({
        .out = &m_input,
        .fontSize = 12.0F,
        .controlHeight = 34.0F,
        .horizontalPadding = 0.0F,
        .clearButtonEnabled = false,
        .frameVisible = false,
        .onChange = [this](const std::string& text) {
          m_placeholder->setVisible(text.empty() && !m_focused);
          if (m_opts.onEdited) m_opts.onEdited(text);
          if (m_binding && m_opts.applyOnEdit) {
            if (m_opts.convert) {
              if (auto v = m_opts.convert(text)) m_binding->set(*v);
            } else {
              m_binding->set(text);
            }
          }
        },
        .onSubmit = [this](const std::string& text) {
          if (m_opts.onAccepted) m_opts.onAccepted(text);
          if (m_binding && !m_opts.applyOnEdit) {
            if (m_opts.convert) {
              if (auto v = m_opts.convert(text)) m_binding->set(*v);
            } else {
              m_binding->set(text);
            }
          }
        },
    }));
    {
      m_input->setOnFocusGain([this]() {
        m_focused = true;
        m_placeholder->setVisible(false);
        restyle(true);
      });
      m_input->setOnFocusLoss([this]() {
        m_focused = false;
        m_placeholder->setVisible(m_input->value().empty());
        restyle(true);
        sync(); // The edit wasn't applied, so show the stored value again.
      });
    }
    restyle(false);
    sync();
  }

  const std::string& Field::text() const { return m_input->value(); }

  void Field::setText(const std::string& s) {
    if (m_input->value() == s) return;
    m_input->setValue(s);
    m_placeholder->setVisible(s.empty() && !m_focused);
  }

  void Field::setPlaceholder(const std::string& s) {
    if (m_placeholder->setText(s)) requestLayout();
  }

  InputArea* Field::focusArea() const { return m_input->inputArea(); }

  void Field::sync() {
    if (!m_binding || m_focused) return;
    const json v = m_binding->get();
    setText(v.is_string() ? v.get<std::string>() : v.is_null() ? std::string() : v.dump());
  }

  void Field::restyle(bool animate) {
    const ColorSpec border = m_focused ? accent(0.7F) : textA(0.08F);
    if (animate) {
      tweenColor(*m_bg, m_border, border, 160, [this](const ColorSpec& c) { m_bg->setBorder(c, 1.0F); });
    } else {
      m_bg->setBorder(border, 1.0F);
    }
    m_border = border;
  }

  float Field::place(Renderer& renderer, float width) {
    const float w = widthFor(width);
    const float h = 34.0F;
    setSize(w, h);
    m_bg->setSize(w, h);
    m_bg->setRadius(h / 2.0F);
    const float left = m_icon != nullptr ? 34.0F : 14.0F;
    if (m_icon != nullptr) {
      m_icon->measure(renderer);
      m_icon->setPosition(12.0F, std::round((h - m_icon->height()) / 2.0F));
    }
    m_placeholder->setMaxWidth(std::max(1.0F, w - left - 14.0F));
    m_placeholder->measure(renderer);
    m_placeholder->setPosition(left, std::round((h - m_placeholder->height()) / 2.0F));
    // Input draws its text 3 px in from its own left edge.
    m_input->setPosition(left - 3.0F, 0.0F);
    m_input->setSize(std::max(1.0F, w - left - 14.0F + 3.0F), h);
    m_input->layout(renderer);
    return h;
  }

  std::unique_ptr<Field> textField(const std::string& path, float width, std::string placeholder, bool allowEmpty) {
    return std::make_unique<Field>(
        bind(path), FieldOpts{
                        .width = width,
                        .placeholder = std::move(placeholder),
                        .convert = [allowEmpty](const std::string& t) -> std::optional<json> {
                          std::size_t a = 0, b = t.size();
                          while (a < b && std::isspace(static_cast<unsigned char>(t[a]))) ++a;
                          while (b > a && std::isspace(static_cast<unsigned char>(t[b - 1]))) --b;
                          if (a == b && !allowEmpty) return std::nullopt;
                          return json(t.substr(a, b - a));
                        },
                    }
    );
  }

  // Swatch

  Swatch::Swatch(std::function<ColorSpec()> color, std::function<bool()> on, std::function<void()> onClick, float size,
                 float ringWidth)
      : m_color(std::move(color)), m_on(std::move(on)), m_onClick(std::move(onClick)), m_size(size), m_ring(ringWidth) {
    m_dot = addBox(*this);
    setCursorShape(kPointer);
    setOnEnter([this](const PointerData&) { tweenScale(*this, 1.12F, 160, 1.70158F); });
    setOnLeave([this]() { tweenScale(*this, 1.0F, 160, 1.70158F); });
    setOnClick([this](const PointerData&) {
      if (m_onClick) m_onClick();
    });
    sync();
  }

  void Swatch::sync() {
    m_dot->setFill(m_color());
    const bool on = m_on && m_on();
    m_dot->setBorder(textA(1.0F), on ? m_ring : 0.0F);
  }

  float Swatch::place(Renderer& /*renderer*/, float /*width*/) {
    setSize(m_size, m_size);
    m_dot->setSize(m_size, m_size);
    m_dot->setRadius(m_size / 2.0F);
    return m_size;
  }

  // ColorPick

  namespace {
    const std::vector<std::string>& colorTokens() {
      static const std::vector<std::string> t{"accent", "accent2", "text", "dim", "faint", "bg",
                                              "card",   "danger",  "warn", "ok",  "transparent"};
      return t;
    }
    std::string firstColor(const json& v) {
      if (v.is_array() && !v.empty() && v[0].is_string()) return v[0].get<std::string>();
      if (v.is_string()) return v.get<std::string>();
      return {};
    }
  } // namespace

  ColorPick::ColorPick(Binding binding) : m_binding(std::move(binding)) {
    m_preview = addBox(*this);
    m_preview->setBorder(textA(0.25F), 1.0F);
    auto field = std::make_unique<Field>(
        Binding{
            .get = [this]() -> json {
              const json v = m_binding.get();
              if (v.is_array()) {
                std::string s;
                for (const auto& e : v) {
                  if (!s.empty()) s += ", ";
                  s += e.is_string() ? e.get<std::string>() : e.dump();
                }
                return s;
              }
              return v.is_null() ? json("") : v;
            },
            .set = [this](const json& v) { m_binding.set(v); },
        },
        FieldOpts{
            .width = 150.0F,
            .placeholder = "token / #hex",
            // "a, b" is a gradient.
            .convert = [](const std::string& t) -> std::optional<json> {
              std::vector<std::string> parts;
              std::size_t start = 0;
              while (start <= t.size()) {
                const auto comma = t.find(',', start);
                std::string p = t.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                std::size_t a = 0, b = p.size();
                while (a < b && std::isspace(static_cast<unsigned char>(p[a]))) ++a;
                while (b > a && std::isspace(static_cast<unsigned char>(p[b - 1]))) --b;
                if (b > a) parts.push_back(p.substr(a, b - a));
                if (comma == std::string::npos) break;
                start = comma + 1;
              }
              if (parts.empty()) return json(nullptr);
              if (parts.size() == 1) return json(parts[0]);
              return json(parts);
            },
        }
    );
    m_field = static_cast<Field*>(addChild(std::move(field)));
    for (const auto& token : colorTokens()) {
      auto area = ui::inputArea({.cursorShape = kPointer, .onClick = [this, token](const InputArea::PointerData&) {
                                   const json cur = m_binding.get();
                                   m_binding.set(cur == json(token) ? json(nullptr) : json(token));
                                 }});
      InputArea* a = static_cast<InputArea*>(addChild(std::move(area)));
      Box* dot = addBox(*a);
      m_tokens.emplace_back(token, dot);
      m_tokenAreas.push_back(a);
    }
    sync();
  }

  void ColorPick::sync() {
    const json cur = m_binding.get();
    const std::string first = firstColor(cur);
    m_preview->setFill(first.empty() ? clearColorSpec() : kusanagi::color(first));
    for (auto& [token, dot] : m_tokens) {
      dot->setFill(kusanagi::color(token));
      const bool on = cur == json(token);
      dot->setBorder(on ? accent() : textA(0.25F), on ? 2.0F : 1.0F);
    }
  }

  float ColorPick::place(Renderer& renderer, float width) {
    const float fh = m_field->place(renderer, width);
    const float h = std::max(26.0F, fh);
    m_preview->setSize(26.0F, 26.0F);
    m_preview->setRadius(13.0F);
    m_preview->setPosition(0.0F, std::round((h - 26.0F) / 2.0F));
    m_field->setPosition(34.0F, std::round((h - fh) / 2.0F));
    float x = 34.0F + m_field->width() + 8.0F;
    for (std::size_t i = 0; i < m_tokenAreas.size(); ++i) {
      m_tokenAreas[i]->setPosition(x, std::round((h - 16.0F) / 2.0F));
      m_tokenAreas[i]->setSize(16.0F, 16.0F);
      m_tokens[i].second->setSize(16.0F, 16.0F);
      m_tokens[i].second->setRadius(8.0F);
      x += 16.0F + 3.0F;
    }
    setSize(x - 3.0F, h);
    return h;
  }

  // PathField

  PathField::PathField(const std::string& path, float width, std::string placeholder) {
    m_field = static_cast<Field*>(addChild(textField(path, width, std::move(placeholder))));
    m_open = static_cast<IconButton*>(addChild(std::make_unique<IconButton>(
        0xf024b, [path]() { spawn({"xdg-open", expandHome(get<std::string>(path, ""))}); }, 34.0F
    )));
  }

  float PathField::place(Renderer& renderer, float width) {
    m_field->place(renderer, width);
    m_open->place(renderer, width);
    m_field->setPosition(0.0F, 0.0F);
    m_open->setPosition(m_field->width() + 6.0F, 0.0F);
    setSize(m_field->width() + 6.0F + 34.0F, 34.0F);
    return 34.0F;
  }

} // namespace kusanagi::sp
