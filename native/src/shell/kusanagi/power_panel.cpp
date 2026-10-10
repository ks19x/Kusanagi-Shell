#include "shell/kusanagi/power_panel.h"

#include "config/config_types.h"
#include "core/deferred_call.h"
#include "core/process/process.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/panel/panel_manager.h"
#include "shell/session/session_action_runner.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"
#include "ui/palette.h"

#include "cursor-shape-v1-client-protocol.h"
#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <cmath>
#include <cctype>

namespace {

  constexpr std::array<std::tuple<char32_t, const char*, char, bool, const char*>, 5> kItems{{
      {0xf033e, "Lock", 'L', false, "lock"},
      {0xf0343, "Log out", 'E', true, "logout"},
      {0xf04b2, "Suspend", 'S', false, "suspend"},
      {0xf0709, "Reboot", 'R', true, "reboot"},
      {0xf0425, "Shut down", 'P', true, "shutdown"},
  }};

  std::string utf8(char32_t c) {
    std::string out;
    out += static_cast<char>(0xF0 | (c >> 18));
    out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
    return out;
  }

  // OutBack easing with overshoot `s`.
  float outBack(float t, float s) {
    const float f = t - 1.0F;
    return 1.0F + (s + 1.0F) * f * f * f + s * f * f;
  }

  // Interpolates every channel, alpha included, on its own.
  Color mix(const Color& a, const Color& b, float t) {
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
  }

  std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }

} // namespace

void KusanagiPowerPanel::create() {
  auto root = ui::node({});
  auto backdrop = ui::inputArea({});
  m_backdropArea = backdrop.get();
  backdrop->addChild(ui::box({.out = &m_backdrop}));
  backdrop->setOnClick([](const InputArea::PointerData&) { PanelManager::instance().closePanel(); });
  root->addChild(std::move(backdrop));
  root->addChild(ui::box({.out = &m_shadow}));

  auto card = ui::inputArea({}); // swallows clicks so they don't reach the backdrop
  m_cardNode = card.get();
  card->addChild(ui::box({.out = &m_card}));
  for (std::size_t i = 0; i < kItems.size(); ++i) {
    auto& b = m_buttons[i];
    auto area = ui::inputArea({.cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER});
    b.area = area.get();
    area->addChild(ui::box({.out = &b.face}));
    area->addChild(ui::label({.out = &b.icon, .maxLines = 1}));
    area->addChild(ui::label({.out = &b.label, .maxLines = 1}));
    area->addChild(ui::label({.out = &b.hint, .maxLines = 1}));
    area->setOnEnter([this, i](const InputArea::PointerData&) {
      m_sel = i;
      m_buttons[i].hovered = true;
      PanelManager::instance().requestLayout();
    });
    area->setOnLeave([this, i]() {
      m_buttons[i].hovered = false;
      PanelManager::instance().requestLayout();
    });
    // Pressing shrinks the face.
    area->setOnPress([](const InputArea::PointerData&) { PanelManager::instance().requestLayout(); });
    area->setOnClick([this, i](const InputArea::PointerData&) { pick(i); });
    card->addChild(std::move(area));
  }
  root->addChild(std::move(card));
  root->addChild(ui::label({.out = &m_title, .maxLines = 1}));
  setRoot(std::move(root));
}

void KusanagiPowerPanel::onOpen(std::string_view /*context*/) {
  m_style = kusanagi::opt<std::string>("power", "style", "row");
  m_sel = 0;
  m_armed = -1;
  m_closing = false;
  for (auto& b : m_buttons) {
    if (m_animations != nullptr) {
      m_animations->cancel(b.faceAnim);
      m_animations->cancel(b.scaleAnim);
    }
    b.faceAnim = b.scaleAnim = 0;
    b.faceSet = false; // open on its colours, without a fade from last time
    b.scale = b.scaleTarget = 1.0F;
    b.hovered = false;
  }
  animateTo(1.0F);
}

void KusanagiPowerPanel::animateFace(Button& b, const Color& target, float durationMs) {
  if (!b.faceSet || m_animations == nullptr) {
    b.faceSet = true;
    b.faceShown = b.faceTarget = target;
  } else if (!(target == b.faceTarget)) {
    // Fade from wherever the colour is now.
    m_animations->cancel(b.faceAnim);
    const Color from = b.faceShown;
    b.faceTarget = target;
    b.faceAnim = m_animations->animate(
        0.0F, 1.0F, durationMs, Easing::Linear,
        [&b, from, target](float t) {
          b.faceShown = mix(from, target, t);
          b.face->setFill(b.faceShown);
        },
        [&b]() { b.faceAnim = 0; }, b.face
    );
  }
  b.face->setFill(b.faceShown);
}

void KusanagiPowerPanel::animateScale(Button& b, float target, bool spring) {
  if (target == b.scaleTarget) return;
  b.scaleTarget = target;
  if (m_animations == nullptr) {
    b.scale = target;
    return;
  }
  m_animations->cancel(b.scaleAnim);
  // Round faces spring with an overshoot, tiles scale linearly.
  const float from = b.scale;
  const float overshoot = kusanagi::bounce(2.0F);
  b.scaleAnim = m_animations->animate(
      0.0F, 1.0F, spring ? 220.0F : 160.0F, Easing::Linear,
      [&b, from, target, spring, overshoot](float t) {
        b.scale = from + (target - from) * (spring ? outBack(t, overshoot) : t);
        PanelManager::instance().requestLayout();
      },
      [&b]() { b.scaleAnim = 0; }, b.face
  );
}

void KusanagiPowerPanel::onClose() {
  m_disarm.stop();
  m_armed = -1;
}

void KusanagiPowerPanel::animateTo(float target, std::function<void()> done) {
  if (m_animations == nullptr) {
    m_open = m_scaleT = target;
    if (done) done();
    return;
  }
  m_animations->cancel(m_anim);
  m_animations->cancel(m_animScale);
  const bool opening = target > 0.5F;
  const float from = m_open;
  m_anim = m_animations->animate(from, target, opening ? 200.0F : 140.0F, Easing::Linear, [this](float v) {
    m_open = v;
    PanelManager::instance().requestLayout();
  });
  // The scale springs in (OutBack) and eases out (InCubic).
  const float overshoot = kusanagi::bounce(1.4F);
  const float scaleFrom = m_scaleT;
  m_animScale = m_animations->animate(
      0.0F, 1.0F, opening ? 420.0F : 160.0F, Easing::Linear,
      [this, opening, overshoot, scaleFrom, target](float t) {
        const float e = opening ? outBack(t, overshoot) : t * t * t;
        m_scaleT = scaleFrom + (target - scaleFrom) * e;
        PanelManager::instance().requestLayout();
      },
      std::move(done)
  );
}

bool KusanagiPowerPanel::beginCloseAnimation(std::function<void()> done) {
  m_closing = true;
  animateTo(0.0F, std::move(done));
  return true;
}

void KusanagiPowerPanel::step(int d) {
  const int n = static_cast<int>(kItems.size());
  m_sel = static_cast<std::size_t>((static_cast<int>(m_sel) + d + n) % n);
  PanelManager::instance().requestLayout();
}

void KusanagiPowerPanel::pick(std::size_t i) {
  if (i >= kItems.size()) return;
  const auto& [icon, label, hint, confirm, action] = kItems[i];
  if (confirm && m_armed != static_cast<int>(i)) {
    m_armed = static_cast<int>(i);
    m_disarm.start(std::chrono::milliseconds(3000), [this]() {
      m_armed = -1;
      PanelManager::instance().requestLayout();
    });
    PanelManager::instance().requestLayout();
    return;
  }
  m_disarm.stop();
  const std::string what = action;
  SessionActionRunner* runner = m_runner;
  DeferredCall::callLater([runner, what]() {
    PanelManager::instance().closePanel(/*animateClose=*/false);
    // hyprlock unless Kusanagi's own lock is chosen in the lock settings.
    if (what == "lock" && kusanagi::opt<std::string>("lock", "engine", "hyprlock") == "hyprlock") {
      (void)process::runAsync("pidof hyprlock || hyprlock || swaylock -f");
      return;
    }
    if (runner != nullptr) runner->invoke(SessionPanelActionConfig{.action = what});
  });
}

bool KusanagiPowerPanel::handleGlobalKey(std::uint32_t sym, std::uint32_t /*modifiers*/, bool pressed, bool preedit) {
  if (!pressed || preedit) return false;
  const bool list = m_style == "list";
  switch (sym) {
  case XKB_KEY_Escape: PanelManager::instance().closePanel(); return true;
  case XKB_KEY_Tab: step(1); return true;
  case XKB_KEY_ISO_Left_Tab: step(-1); return true;
  case XKB_KEY_Right: if (!list) step(1); return !list;
  case XKB_KEY_Left: if (!list) step(-1); return !list;
  case XKB_KEY_Down: if (list) step(1); return list;
  case XKB_KEY_Up: if (list) step(-1); return list;
  case XKB_KEY_Return:
  case XKB_KEY_KP_Enter:
  case XKB_KEY_space: pick(m_sel); return true;
  default: break;
  }
  const std::uint32_t upper = (sym >= XKB_KEY_a && sym <= XKB_KEY_z) ? sym - XKB_KEY_a + 'A' : sym;
  for (std::size_t i = 0; i < kItems.size(); ++i) {
    if (static_cast<std::uint32_t>(std::get<2>(kItems[i])) == upper) {
      m_sel = i;
      pick(i);
      return true;
    }
  }
  return false;
}

void KusanagiPowerPanel::doLayout(Renderer& renderer, float width, float height) {
  auto* rootNode = root();
  if (rootNode == nullptr) return;
  m_width = width;
  m_height = height;
  rootNode->setSize(width, height);
  const float s = contentScale();
  const bool list = m_style == "list", tiles = m_style == "tiles", full = m_style == "fullscreen", pill = m_style == "pill";
  const std::string font = kusanagi::font();

  // Backdrop
  const float dim = full ? 0.78F : std::min(0.6F, static_cast<float>(kusanagi::opt<double>("look", "backdrop", 0.25)) * 1.6F);
  m_backdropArea->setPosition(0.0F, 0.0F);
  m_backdropArea->setSize(width, height);
  m_backdrop->setSize(width, height);
  m_backdrop->setFill(ColorSpec{.role = std::nullopt, .fixed = rgba(0.0F, 0.0F, 0.0F, 1.0F), .alpha = dim * m_open});

  // Buttons
  const float bw = (list ? 236.0F : tiles ? 118.0F : full ? 150.0F : pill ? 48.0F : 84.0F) * s;
  const float bh = (list ? 42.0F : tiles ? 118.0F : full ? 190.0F : pill ? 48.0F : 118.0F) * s;
  const float spacing = (list ? 2.0F : full ? 44.0F : pill ? 6.0F : tiles ? 10.0F : 14.0F) * s;
  const float padH = (list ? 8.0F : pill ? 10.0F : full ? 0.0F : 24.0F) * s;
  const float padV = (list ? 8.0F : pill ? 8.0F : full ? 0.0F : 22.0F) * s;
  const std::size_t n = kItems.size();
  const float gridW = list ? bw : bw * n + spacing * (n - 1);
  const float gridH = list ? bh * n + spacing * (n - 1) : bh;
  const float cw = gridW + 2 * padH, ch = gridH + 2 * padV;

  const float barThickness = 28.0F * s;
  const float cx = list ? width - cw - 14.0F * s : std::round((width - cw) / 2);
  const float cy = list ? barThickness + 8.0F * s : pill ? height - ch - 64.0F * s : std::round((height - ch) / 2);
  const float radius = (pill ? ch / 2 : list ? std::max(10.0F, kusanagi::radius() - 2.0F) * s : kusanagi::radius() * s);

  // Open and close: opacity, and scale around the anchor (top right for list, bottom for pill, else centre).
  const float scale0 = full ? 1.06F : 0.92F;
  const float scale = scale0 + (1.0F - scale0) * m_scaleT;
  m_cardNode->setPosition(cx, cy);
  m_cardNode->setSize(cw, ch);
  m_cardNode->setTransformOrigin(list ? cw : cw / 2, list ? 0.0F : pill ? ch : ch / 2);
  m_cardNode->setScale(scale);
  m_cardNode->setOpacity(m_open);

  m_card->setSize(cw, ch);
  m_card->setRadius(radius);
  m_card->setFill(full ? clearColorSpec() : colorSpecFromRole(ColorRole::Surface, static_cast<float>(kusanagi::opt<double>("panel", "opacity", 0.95))));
  m_card->setBorder(full ? clearColorSpec() : kusanagi::surfaceBorder(), full ? 0.0F : kusanagi::surfaceBorderWidth() * s);

  m_shadow->setVisible(kusanagi::shadows() && !full);
  m_shadow->setPosition(cx, cy + 14.0F * s);
  m_shadow->setSize(cw, ch);
  m_shadow->setRadius(radius);
  m_shadow->setSoftness(40.0F * s);
  m_shadow->setFill(ColorSpec{.role = std::nullopt, .fixed = rgba(0.0F, 0.0F, 0.0F, 1.0F), .alpha = 0.5F * m_open});

  const ColorSpec text = colorSpecFromRole(ColorRole::OnSurface);
  const ColorSpec ink = colorSpecFromRole(ColorRole::Surface);
  for (std::size_t i = 0; i < n; ++i) {
    auto& b = m_buttons[i];
    const auto& [icon, label, hint, confirm, action] = kItems[i];
    const bool current = m_sel == i;
    const bool armed = m_armed == static_cast<int>(i);
    const ColorSpec face = armed ? colorSpecFromRole(ColorRole::Error)
        : current                ? colorSpecFromRole(ColorRole::Primary)
                                 : colorSpecFromRole(ColorRole::OnSurface, b.hovered ? 0.1F : 0.06F);
    const ColorSpec glyphInk = current || armed ? ink : text;
    const float bx = padH + (list ? 0.0F : i * (bw + spacing));
    const float by = padV + (list ? i * (bh + spacing) : 0.0F);
    b.area->setPosition(bx, by);
    b.area->setSize(bw, bh);

    b.icon->setText(utf8(icon));
    b.icon->setFontFamily(font);
    b.label->setFontFamily(font);
    b.hint->setFontFamily(font);
    b.hint->setVisible(list);

    if (list) {
      b.face->setPosition(0.0F, 0.0F);
      b.face->setSize(bw, bh);
      b.face->setRadius(9.0F * s);
      b.face->setScale(1.0F);
      animateFace(
          b,
          resolveColorSpec(
              armed     ? colorSpecFromRole(ColorRole::Error)
              : current ? colorSpecFromRole(ColorRole::Primary, 0.9F)
              : b.hovered ? colorSpecFromRole(ColorRole::OnSurface, 0.07F)
                          : clearColorSpec()
          ),
          120.0F
      );
      b.face->setBorder(clearColorSpec(), 0.0F);
      b.icon->setFontSize(17.0F * s);
      b.icon->setColor(glyphInk);
      b.icon->measure(renderer);
      b.icon->setPosition(14.0F * s, std::round((bh - b.icon->height()) / 2));
      b.label->setText(armed ? "Click again to " + lower(label) : std::string(label));
      b.label->setFontSize(13.0F * s);
      b.label->setFontWeight(current ? FontWeight::Bold : FontWeight::Normal);
      b.label->setColor(glyphInk);
      b.label->measure(renderer);
      b.label->setPosition(44.0F * s, std::round((bh - b.label->height()) / 2));
      b.hint->setText(std::string(1, hint));
      b.hint->setFontSize(11.0F * s);
      b.hint->setColor(current || armed ? colorSpecFromRole(ColorRole::Surface, 0.7F) : colorSpecFromRole(ColorRole::OnSurfaceVariant));
      b.hint->measure(renderer);
      b.hint->setPosition(bw - 14.0F * s - b.hint->width(), std::round((bh - b.hint->height()) / 2));
      b.label->setVisible(true);
      continue;
    }
    if (tiles) {
      b.face->setPosition(0.0F, 0.0F);
      b.face->setSize(bw, bh);
      b.face->setRadius(std::max(10.0F, kusanagi::radius() - 4.0F) * s);
      animateFace(b, resolveColorSpec(face), 160.0F);
      animateScale(b, b.area->pressed() ? 0.95F : 1.0F, false);
      // The tile's icon and name scale with it.
      b.face->setTransformOrigin(bw / 2, bh / 2);
      b.face->setScale(b.scale);
      b.face->setBorder(current && !armed ? clearColorSpec() : colorSpecFromRole(ColorRole::OnSurface, 0.08F), current && !armed ? 0.0F : 1.0F * s);
      b.icon->setFontSize(34.0F * s);
      b.icon->setColor(glyphInk);
      b.icon->measure(renderer);
      b.icon->setPosition(std::round((bw - b.icon->width()) / 2), std::round((bh - b.icon->height()) / 2 - 12.0F * s));
      b.label->setText(armed ? "Again?" : label);
      b.label->setFontSize(12.0F * s);
      b.label->setFontWeight(FontWeight::Bold);
      b.label->setColor(glyphInk);
      b.label->measure(renderer);
      b.label->setPosition(std::round((bw - b.label->width()) / 2), bh - 14.0F * s - b.label->height());
      b.label->setVisible(true);
      for (Label* l : {b.icon, b.label}) {
        l->setTransformOrigin(bw / 2 - l->x(), bh / 2 - l->y());
        l->setScale(b.scale);
      }
      continue;
    }
    // Row, fullscreen and pill: a circle with the name under it (none on the pill).
    const float d = (full ? 150.0F : pill ? 48.0F : 84.0F) * s;
    animateScale(b, b.area->pressed() ? 0.92F : current ? 1.05F : 1.0F, true);
    b.face->setSize(d, d);
    b.face->setPosition(std::round((bw - d) / 2), 0.0F);
    b.face->setRadius(d / 2);
    b.face->setTransformOrigin(d / 2, d / 2);
    b.face->setScale(b.scale);
    animateFace(b, resolveColorSpec(face), 160.0F);
    b.face->setBorder(colorSpecFromRole(ColorRole::OnSurface, full ? 0.15F : 0.08F), current && !armed ? 0.0F : 1.0F * s);
    b.icon->setFontSize((full ? 52.0F : pill ? 20.0F : 30.0F) * s);
    b.icon->setColor(glyphInk);
    b.icon->measure(renderer);
    b.icon->setPosition(std::round((bw - b.icon->width()) / 2), std::round((d - b.icon->height()) / 2));
    // The glyph scales with the circle, about the circle's centre.
    b.icon->setTransformOrigin(bw / 2 - b.icon->x(), d / 2 - b.icon->y());
    b.icon->setScale(b.scale);
    b.label->setVisible(!pill);
    if (!pill) {
      b.label->setText(armed ? "Click again" : label);
      b.label->setFontSize((full ? 17.0F : 12.0F) * s);
      b.label->setFontWeight(current || armed ? FontWeight::Bold : FontWeight::Normal);
      b.label->setColor(armed ? colorSpecFromRole(ColorRole::Error) : text);
      b.label->measure(renderer);
      b.label->setPosition(std::round((bw - b.label->width()) / 2), bh - b.label->height());
    }
  }

  // Pill: what Enter would do, shown above it.
  m_title->setVisible(pill);
  if (pill) {
    const auto& item = kItems[m_armed >= 0 ? static_cast<std::size_t>(m_armed) : m_sel];
    m_title->setText(m_armed >= 0 ? "Click again to " + lower(std::get<1>(item)) : std::string(std::get<1>(item)));
    m_title->setFontFamily(font);
    m_title->setFontSize(13.0F * s);
    m_title->setFontWeight(FontWeight::Bold);
    m_title->setColor(m_armed >= 0 ? colorSpecFromRole(ColorRole::Error) : text);
    m_title->measure(renderer);
    m_title->setPosition(std::round((width - m_title->width()) / 2), cy - 10.0F * s - m_title->height());
    m_title->setOpacity(m_open);
  }
}
