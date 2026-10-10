// Style picker: one card per option with a small drawing of a screen in that style, made of plain
// rectangles. Geometry and colours match the classic Kusanagi look.

#include "cursor-shape-v1-client-protocol.h"
#include "render/core/color.h"
#include "render/core/render_styles.h"
#include "render/core/renderer.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <cmath>

namespace kusanagi::sp {

  namespace {

    // Adds a rectangle to the little screen.
    Box* rect(Node& parent, float x, float y, float w, float h, float r, const ColorSpec& c) {
      auto* b = static_cast<Box*>(parent.addChild(ui::box({})));
      b->setPosition(x, y);
      b->setSize(std::max(0.0F, w), std::max(0.0F, h));
      b->setRadius(std::min(r, std::min(w, h) / 2.0F));
      b->setFill(c);
      return b;
    }

    ColorSpec surf() { return bgPanel(0.92F); }
    ColorSpec line() { return textA(0.35F); }
    ColorSpec acc() { return accent(); }

    // n placeholder text lines, the first one longer.
    void lines(Node& p, float x, float y, float w, int n) {
      for (int i = 0; i < n; ++i) rect(p, x, y + static_cast<float>(i) * 5.0F, w * (i == 0 ? 0.8F : 0.55F), 2.0F, 1.0F, textA(0.35F));
    }

    // Grid of identical cells.
    void grid(Node& p, float x, float y, int columns, float spacing, int count, float w, float h, float r,
              const std::function<ColorSpec(int)>& color) {
      for (int i = 0; i < count; ++i) {
        const int c = i % columns;
        const int row = i / columns;
        rect(p, x + static_cast<float>(c) * (w + spacing), y + static_cast<float>(row) * (h + spacing), w, h, r, color(i));
      }
    }

    void drawLauncher(Node& s, float W, float H, const std::string& v) {
      if (v == "card") {
        Box* c = rect(s, W * 0.22F, H * 0.16F, W * 0.56F, H * 0.62F, 4.0F, surf());
        rect(*c, 5.0F, 5.0F, c->width() - 10.0F, 6.0F, 3.0F, textA(0.15F));
        lines(*c, 6.0F, 17.0F, c->width() - 12.0F, 4);
      } else if (v == "spotlight") {
        Box* c = rect(s, W * 0.2F, H * 0.32F, W * 0.6F, 12.0F, 6.0F, surf());
        rect(*c, 6.0F, 5.0F, 3.0F, 3.0F, 1.5F, acc());
        rect(*c, 13.0F, 5.0F, c->width() * 0.4F, 2.0F, 1.0F, line());
      } else if (v == "fullscreen") {
        Box* c = rect(s, 0.0F, 0.0F, W, H, 0.0F, surf());
        rect(*c, W * 0.3F, 6.0F, W * 0.4F, 6.0F, 3.0F, textA(0.15F));
        grid(*c, W * 0.14F, 20.0F, 6, 7.0F, 18, 8.0F, 8.0F, 2.0F, [](int i) { return i == 0 ? acc() : textA(0.3F); });
      } else if (v == "side") {
        Box* c = rect(s, 0.0F, 0.0F, W * 0.36F, H, 0.0F, surf());
        rect(*c, 5.0F, 5.0F, c->width() - 10.0F, 6.0F, 3.0F, textA(0.15F));
        lines(*c, 6.0F, 17.0F, c->width() - 12.0F, 6);
      }
    }

    void drawPanel(Node& s, float W, float H, const std::string& v) {
      // Top bar with the clock in the middle.
      rect(s, 0.0F, 0.0F, W, 6.0F, 0.0F, bgPanel(0.6F));
      if (v != "sheet") rect(s, W / 2.0F - 8.0F, 1.0F, 16.0F, 4.0F, 2.0F, v == "island" ? acc() : line());
      if (v == "island" || v == "fade") {
        Box* c = rect(s, W * 0.27F, v == "island" ? 8.0F : 12.0F, W * 0.46F, H * 0.62F, 5.0F, surf());
        c->setOpacity(v == "fade" ? 0.75F : 1.0F);
        if (v == "island") rect(*c, c->width() / 2.0F - 8.0F, -3.0F, 16.0F, 4.0F, 2.0F, acc());
        grid(*c, 6.0F, 7.0F, 3, 4.0F, 6, 14.0F, 9.0F, 2.0F, [](int) { return textA(0.18F); });
      } else if (v == "drop") {
        Box* c = rect(s, 0.0F, 6.0F, W, H * 0.5F, 0.0F, surf());
        grid(*c, c->width() * 0.3F, 6.0F, 4, 4.0F, 8, 10.0F, 7.0F, 2.0F, [](int) { return textA(0.18F); });
      } else if (v == "sheet") {
        Box* c = rect(s, W * 0.6F, 8.0F, W * 0.37F, H - 12.0F, 4.0F, surf());
        grid(*c, 5.0F, 6.0F, 2, 4.0F, 6, 16.0F, 8.0F, 2.0F, [](int) { return textA(0.18F); });
      }
    }

    void drawNotifications(Node& s, float W, float /*H*/, const std::string& v) {
      // Two toasts in the top-right corner.
      const bool roomy = v == "comfortable" || v == "accent";
      const float colW = W * 0.54F;
      const float h = roomy ? 22.0F : v == "compact" ? 15.0F : 11.0F;
      const float spacing = roomy ? 5.0F : 4.0F;
      for (int i = 0; i < 2; ++i) {
        Box* t = rect(s, W * 0.42F, 6.0F + static_cast<float>(i) * (h + spacing), colW, h, v == "minimal" ? h / 2.0F : 4.0F, surf());
        const float tx = v == "accent" ? 9.0F : 6.0F;
        if (v == "accent") rect(*t, 3.0F, 4.0F, 2.0F, h - 8.0F, 1.0F, acc());
        rect(*t, tx, 4.0F, 14.0F, 2.0F, 1.0F, v == "minimal" ? textA(1.0F) : acc());
        if (v != "minimal") rect(*t, tx, 9.0F, colW * 0.6F, 2.0F, 1.0F, line());
        if (roomy) rect(*t, tx, 14.0F, colW * 0.45F, 2.0F, 1.0F, line());
        if (v == "minimal") rect(*t, 24.0F, 5.0F, colW * 0.45F, 2.0F, 1.0F, line());
      }
    }

    void drawOsd(Node& s, float W, float H, const std::string& v) {
      if (v == "pill") {
        Box* p = rect(s, W * 0.25F, 10.0F, W * 0.5F, 12.0F, 6.0F, surf());
        rect(*p, 3.0F, 2.0F, 8.0F, 8.0F, 4.0F, acc());
        Box* track = rect(*p, 15.0F, 5.0F, p->width() - 22.0F, 2.0F, 1.0F, textA(0.2F));
        rect(*track, 0.0F, 0.0F, track->width() * 0.6F, 2.0F, 1.0F, acc());
      } else if (v == "minimal") {
        Box* p = rect(s, W * 0.22F, 12.0F, W * 0.56F, 7.0F, 3.5F, surf());
        Box* track = rect(*p, 6.0F, 2.5F, p->width() - 12.0F, 2.0F, 1.0F, textA(0.2F));
        rect(*track, 0.0F, 0.0F, track->width() * 0.6F, 2.0F, 1.0F, acc());
      } else if (v == "box") {
        Box* b = rect(s, W / 2.0F - 18.0F, H * 0.42F, 36.0F, 30.0F, 6.0F, surf());
        rect(*b, 12.0F, 5.0F, 12.0F, 10.0F, 2.0F, textA(1.0F));
        for (int i = 0; i < 8; ++i) rect(*b, 5.0F + static_cast<float>(i) * 3.6F, 22.0F, 2.6F, 3.0F, 0.0F, i < 5 ? acc() : textA(0.2F));
      }
    }

    void drawTiles(Node& s, float W, float H, const std::string& v) {
      const bool icons = v == "icons";
      const bool pills = v == "pills";
      const int columns = icons ? 5 : pills ? 2 : 4;
      const int count = icons ? 10 : pills ? 6 : 8;
      const float w = icons ? 16.0F : pills ? 50.0F : 24.0F;
      const float h = icons ? 16.0F : pills ? 12.0F : 18.0F;
      const int rows = (count + columns - 1) / columns;
      const float gw = static_cast<float>(columns) * w + static_cast<float>(columns - 1) * 4.0F;
      const float gh = static_cast<float>(rows) * h + static_cast<float>(rows - 1) * 4.0F;
      const float x0 = std::round((W - gw) / 2.0F);
      const float y0 = std::round((H - gh) / 2.0F);
      for (int i = 0; i < count; ++i) {
        Box* t = rect(s, x0 + static_cast<float>(i % columns) * (w + 4.0F), y0 + static_cast<float>(i / columns) * (h + 4.0F),
                      w, h, v == "cards" ? 3.0F : h / 2.0F, i == 0 ? acc() : textA(0.2F));
        if (pills) {
          rect(*t, 4.0F, 5.0F, 3.0F, 3.0F, 1.5F, textA(1.0F));
          rect(*t, 10.0F, 5.0F, 20.0F, 2.0F, 1.0F, line());
        }
        if (v == "cards") {
          rect(*t, 3.0F, 3.0F, 4.0F, 4.0F, 1.0F, textA(1.0F));
          rect(*t, 3.0F, 12.0F, 14.0F, 2.0F, 1.0F, line());
        }
      }
    }

    void drawPanelLook(Node& s, float W, float H, const std::string& v) {
      const float pw = W * 0.62F;
      const float ph = H * 0.86F;
      Box* p = rect(s, std::round((W - pw) / 2.0F), std::round((H - ph) / 2.0F), pw, ph, 5.0F, surf());
      const bool icons = v == "icons";
      const bool compact = v == "compact";
      const bool dash = v == "dashboard";
      const float colW = pw - 10.0F;
      float y = 5.0F;
      if (!icons) {
        const float hh = compact ? 3.0F : 5.0F;
        rect(*p, 5.0F, y, compact ? 16.0F : 22.0F, hh, 1.0F, textA(1.0F));
        y += hh + 3.0F;
      }
      if (dash) {
        rect(*p, 5.0F, y, colW, 14.0F, 3.0F, accent(0.35F));
        y += 14.0F + 3.0F;
      }
      const int columns = icons ? 4 : compact ? 2 : dash ? 3 : 4;
      const int count = icons ? 8 : compact ? 4 : 6;
      const float w = icons ? 10.0F : compact ? 30.0F : dash ? 20.0F : 14.0F;
      const float h = icons ? 10.0F : compact ? 7.0F : 10.0F;
      grid(*p, 5.0F, y, columns, 2.0F, count, w, h, icons || compact ? h / 2.0F : 2.0F, [](int) { return textA(0.22F); });
      const int rows = (count + columns - 1) / columns;
      y += static_cast<float>(rows) * h + static_cast<float>(rows - 1) * 2.0F + 3.0F;
      rect(*p, 5.0F, y, colW, compact || icons ? 2.0F : 6.0F, 3.0F, acc());
    }

    void drawLock(Node& s, float W, float H, const std::string& v) {
      rect(s, 0.0F, 0.0F, W, H, 0.0F, fixedColorSpec(Color{0.0F, 0.0F, 0.0F, 0.35F}));
      if (v == "split") {
        Box* side = rect(s, 0.0F, 0.0F, W * 0.38F, H, 0.0F, surf());
        rect(*side, side->width() - 1.0F, 0.0F, 1.0F, H, 0.0F, acc());
      }
      if (v == "card") rect(s, std::round((W - W * 0.5F) / 2.0F), std::round((H - H * 0.8F) / 2.0F), W * 0.5F, H * 0.8F, 6.0F, surf());
      if (v != "terminal" && v != "stacked") {
        const float w = v == "minimal" ? 18.0F : v == "card" ? 30.0F : 38.0F;
        const float h = v == "minimal" ? 6.0F : 10.0F;
        const float x = v == "split" ? 8.0F : v == "minimal" ? 6.0F : (W - w) / 2.0F;
        const float y = v == "card" ? H * 0.18F : v == "minimal" ? 6.0F : H * 0.16F;
        rect(s, x, y, w, h, 2.0F, textA(1.0F));
      }
      if (v == "stacked") {
        rect(s, 10.0F, H * 0.2F, 24.0F, 16.0F, 2.0F, acc());
        rect(s, 10.0F, H * 0.2F + 19.0F, 24.0F, 16.0F, 2.0F, textA(1.0F));
      }
      const float cx = v == "split" ? W * 0.19F : v == "stacked" ? W * 0.72F : W / 2.0F;
      if (v != "terminal" && v != "minimal") {
        Box* av = rect(s, cx - 4.5F, v == "card" ? H * 0.42F : v == "stacked" ? H * 0.36F : H * 0.55F, 9.0F, 9.0F, 4.5F,
                       clearColorSpec());
        av->setBorder(acc(), 1.0F);
      }
      if (v != "terminal") {
        const float y = v == "card" ? H * 0.62F : v == "minimal" ? H * 0.48F : v == "stacked" ? H * 0.56F : H * 0.72F;
        rect(s, cx - 17.0F, y, 34.0F, 6.0F, 3.0F, textA(0.3F));
      }
      if (v == "terminal") {
        const float y = H * 0.32F;
        rect(s, 12.0F, y, 40.0F, 3.0F, 0.0F, line());
        rect(s, 12.0F, y + 7.0F, 30.0F, 3.0F, 0.0F, textA(1.0F));
        rect(s, 12.0F, y + 14.0F, 22.0F, 3.0F, 0.0F, textA(1.0F));
        rect(s, 36.0F, y + 12.5F, 4.0F, 6.0F, 0.0F, acc());
      }
    }

    void drawPower(Node& s, float W, float H, const std::string& v) {
      rect(s, 0.0F, 0.0F, W, H, 0.0F, fixedColorSpec(Color{0.0F, 0.0F, 0.0F, v == "fullscreen" ? 0.55F : 0.25F}));
      if (v == "row" || v == "tiles") {
        const float bw = W * 0.72F;
        const float bh = v == "tiles" ? 26.0F : 28.0F;
        Box* b = rect(s, std::round((W - bw) / 2.0F), std::round((H - bh) / 2.0F), bw, bh, 5.0F, surf());
        const float rowW = 5.0F * 14.0F + 4.0F * 3.0F;
        for (int i = 0; i < 5; ++i) {
          rect(*b, std::round((bw - rowW) / 2.0F) + static_cast<float>(i) * 17.0F, std::round((bh - 14.0F) / 2.0F), 14.0F, 14.0F,
               v == "tiles" ? 3.0F : 7.0F, i == 0 ? acc() : textA(0.22F));
        }
      } else if (v == "fullscreen") {
        const float rowW = 5.0F * 18.0F + 4.0F * 5.0F;
        for (int i = 0; i < 5; ++i) {
          rect(s, std::round((W - rowW) / 2.0F) + static_cast<float>(i) * 23.0F, std::round((H - 18.0F) / 2.0F), 18.0F, 18.0F,
               9.0F, i == 0 ? acc() : textA(0.25F));
        }
      } else if (v == "list") {
        Box* b = rect(s, W - 50.0F - 5.0F, 5.0F, 50.0F, 44.0F, 4.0F, surf());
        for (int i = 0; i < 5; ++i) rect(*b, 3.0F, 3.0F + static_cast<float>(i) * 8.0F, 44.0F, 6.0F, 2.0F, i == 0 ? acc() : textA(0.2F));
      } else if (v == "pill") {
        Box* b = rect(s, std::round((W - 64.0F) / 2.0F), H - 8.0F - 14.0F, 64.0F, 14.0F, 7.0F, surf());
        const float rowW = 5.0F * 8.0F + 4.0F * 3.0F;
        for (int i = 0; i < 5; ++i) {
          rect(*b, std::round((64.0F - rowW) / 2.0F) + static_cast<float>(i) * 11.0F, 3.0F, 8.0F, 8.0F, 4.0F,
               i == 0 ? acc() : textA(0.25F));
        }
      }
    }

    bool endsWith(const std::string& s, std::string_view e) { return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0; }

    void drawCorner(Node& s, float W, float H, const std::string& v) {
      const float w = W * 0.38F;
      const float h = 14.0F;
      const float x = endsWith(v, "left") ? 5.0F : endsWith(v, "right") ? W - w - 5.0F : (W - w) / 2.0F;
      const float y = v.starts_with("top") ? 5.0F : H - h - 5.0F;
      Box* t = rect(s, x, y, w, h, 4.0F, surf());
      rect(*t, 5.0F, 4.0F, 12.0F, 2.0F, 1.0F, acc());
      rect(*t, 5.0F, 8.0F, w * 0.6F, 2.0F, 1.0F, line());
    }

    void drawEdge(Node& s, float W, float H, const std::string& v) {
      const bool side = v == "left" || v == "right";
      const float w = side ? 9.0F : W * 0.42F;
      const float h = side ? H * 0.6F : 9.0F;
      const float x = v == "left" ? 5.0F : v == "right" ? W - w - 5.0F : (W - w) / 2.0F;
      const float y = v == "top" ? 6.0F : v == "bottom" ? H - h - 6.0F : (H - h) / 2.0F;
      Box* p = rect(s, x, y, w, h, 4.5F, surf());
      rect(*p, side ? 3.5F : 4.0F, side ? h * 0.35F : 3.5F, side ? 2.0F : w * 0.55F, side ? h * 0.6F : 2.0F, 1.0F, acc());
    }

  } // namespace

  struct StylePicker::Card : public InputArea {
    Card(StylePicker& owner, Option opt) : picker(owner), option(std::move(opt)) {
      bg = static_cast<Box*>(addChild(ui::box({})));
      screenBg = static_cast<Box*>(addChild(ui::box({})));
      // Clips to the bounding rectangle, not the rounded one.
      screen = addChild(ui::node({}));
      screen->setClipChildren(true);
      title = static_cast<Label*>(addChild(makeText(option.label, 12.0F, true)));
      title->setMaxLines(1);
      if (!option.note.empty()) {
        note = static_cast<Label*>(addChild(makeText(option.note, 9.0F, false, dim())));
        note->setMaxLines(1);
        note->setEllipsize(TextEllipsize::End);
      }
      setCursorShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
      setOnEnter([this](const PointerData&) { restyle(true); });
      setOnLeave([this]() { restyle(true); });
      setOnPress([this](const PointerData& d) { tweenScale(*this, d.pressed ? 0.97F : 1.0F, 140, 1.70158F); });
      setOnCancel([this]() { tweenScale(*this, 1.0F, 140, 1.70158F); });
      setOnClick([this](const PointerData&) {
        picker.m_binding.set(option.value);
        picker.sync();
      });
    }

    void restyle(bool animate) {
      const ColorSpec fill = textA(hovered() ? 0.08F : 0.04F);
      const ColorSpec border = on ? accent() : textA(0.07F);
      if (animate) {
        tweenColor(*bg, fillColor, fill, 120, [this](const ColorSpec& c) { bg->setFill(c); });
      } else {
        bg->setFill(fill);
      }
      bg->setBorder(border, on ? 2.0F : 1.0F);
      title->setColor(on ? accent() : textA(1.0F));
      fillColor = fill;
    }

    void paintScreen() {
      // The gradient follows the palette, refreshed on build and sync.
      RoundedRectStyle st;
      st.fillMode = FillMode::LinearGradient;
      st.gradientDirection = GradientDirection::Vertical;
      const Color c0 = darker(resolved(accent()), 3.2F);
      const Color c1 = darker(resolved(accent2()), 4.2F);
      st.gradientStops = {GradientStop{0.0F, c0}, GradientStop{1.0F, c1}, GradientStop{1.0F, c1}, GradientStop{1.0F, c1}};
      st.fill = c0;
      st.radius = Radii(6.0F, 6.0F, 6.0F, 6.0F);
      screenBg->setStyle(st);
      screenBg->setSize(screenW, 74.0F);
      screen->setSize(screenW, 74.0F);
    }

    void build(const std::string& kind, float W) {
      screenW = W;
      while (!screen->children().empty()) screen->removeChild(screen->children().back().get());
      const float H = 74.0F;
      const std::string v = option.value.is_string() ? option.value.get<std::string>() : option.value.dump();
      if (kind == "launcher") drawLauncher(*screen, W, H, v);
      else if (kind == "panel") drawPanel(*screen, W, H, v);
      else if (kind == "notifications") drawNotifications(*screen, W, H, v);
      else if (kind == "osd") drawOsd(*screen, W, H, v);
      else if (kind == "tiles") drawTiles(*screen, W, H, v);
      else if (kind == "panelLook") drawPanelLook(*screen, W, H, v);
      else if (kind == "lock") drawLock(*screen, W, H, v);
      else if (kind == "power") drawPower(*screen, W, H, v);
      else if (kind == "corner") drawCorner(*screen, W, H, v);
      else if (kind == "edge") drawEdge(*screen, W, H, v);
      paintScreen();
    }

    StylePicker& picker;
    Option option;
    Box* bg = nullptr;
    Box* screenBg = nullptr;
    Node* screen = nullptr;
    Label* title = nullptr;
    Label* note = nullptr;
    ColorSpec fillColor;
    bool on = false;
    float screenW = 0.0F;
  };

  StylePicker::StylePicker(std::string kind, std::vector<Option> options, Binding binding, float cardW)
      : m_kind(std::move(kind)), m_options(std::move(options)), m_binding(std::move(binding)), m_cardW(cardW) {
    const json cur = m_binding.get();
    for (const auto& o : m_options) {
      auto card = std::make_unique<Card>(*this, o);
      card->on = cur == o.value;
      card->build(m_kind, m_cardW - 18.0F);
      card->restyle(false);
      m_cards.push_back(static_cast<Card*>(addChild(std::move(card))));
    }
  }

  void StylePicker::sync() {
    const json cur = m_binding.get();
    for (Card* c : m_cards) {
      const bool on = cur == c->option.value;
      // The palette may have changed, so redraw the gradient.
      c->paintScreen();
      if (on == c->on) continue;
      c->on = on;
      c->restyle(true);
    }
  }

  float StylePicker::place(Renderer& renderer, float width) {
    width = widthFor(width);
    // Cards flow into rows.
    float x = 0.0F;
    float y = 0.0F;
    float lineH = 0.0F;
    for (Card* c : m_cards) {
      const float w = m_cardW;
      const float h = c->option.note.empty() ? 116.0F : 132.0F;
      if (x > 0.0F && x + w > width + 0.5F) {
        y += lineH + 10.0F;
        x = 0.0F;
        lineH = 0.0F;
      }
      c->setPosition(x, y);
      c->setSize(w, h);
      c->bg->setSize(w, h);
      c->bg->setRadius(12.0F);
      c->screen->setPosition(9.0F, 9.0F);
      c->screenBg->setPosition(9.0F, 9.0F);
      c->title->measure(renderer); // Not elided; long titles overflow.
      c->title->setPosition(12.0F, 90.0F);
      if (c->note != nullptr) {
        c->note->setMaxWidth(w - 24.0F);
        c->note->measure(renderer);
        c->note->setPosition(12.0F, 90.0F + c->title->height() + 1.0F);
      }
      x += w + 10.0F;
      lineH = std::max(lineH, h);
    }
    const float total = y + lineH;
    setSize(width, total);
    return total;
  }

} // namespace kusanagi::sp
