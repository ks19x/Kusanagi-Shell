#include "shell/kusanagi/media_popup.h"

#include "core/deferred_call.h"
#include "core/log.h"
#include "core/ui_phase.h"
#include "dbus/mpris/mpris_art.h"
#include "dbus/mpris/mpris_service.h"
#include "ipc/ipc_service.h"
#include "render/core/renderer.h"
#include "render/render_context.h"
#include "render/scene/input_area.h"
#include "render/scene/node.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"
#include "wayland/popup_surface.h"
#include "wayland/wayland_connection.h"
#include "wayland/wayland_seat.h"

#include "cursor-shape-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cmath>
#include <linux/input-event-codes.h>

// The popup is larger than the card so the drop-in slide and the shadow have room. It opens 280 ms after
// the module is hovered and closes 260 ms after the pointer has left both the module and the card.

namespace {

  constexpr Logger kLog("media-popup");

  constexpr float kWinW = 384.0F;
  constexpr float kWinH = 156.0F;
  constexpr float kCardX = 12.0F;
  constexpr float kCardW = 360.0F;
  constexpr float kCardH = 132.0F;
  constexpr float kArt = 104.0F;
  constexpr float kTextX = 14.0F + kArt + 14.0F;
  constexpr float kTextW = kCardW - kTextX - 14.0F;
  constexpr float kProgressY = 72.0F;
  constexpr const char* kIconFont = "JetBrainsMono Nerd Font";

  constexpr auto kOpenDelay = std::chrono::milliseconds(280);
  constexpr auto kCloseDelay = std::chrono::milliseconds(260);

  float easeLinear(float t) { return t; }
  float easeOutQuint(float t) {
    const float u = 1.0F - t;
    return 1.0F - u * u * u * u * u;
  }

  std::string utf8(char32_t cp) {
    std::string out;
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
    return out;
  }

  std::string escapeMarkup(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (const char c : in) {
      switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
      }
    }
    return out;
  }

  std::string fmt(std::int64_t us) {
    const std::int64_t sec = us / 1000000;
    if (sec <= 0) return "0:00";
    const std::int64_t m = sec / 60;
    const std::int64_t s = sec % 60;
    return std::to_string(m) + ":" + (s < 10 ? "0" : "") + std::to_string(s);
  }

  ColorSpec role(ColorRole r, float a = 1.0F) { return colorSpecFromRole(r, a); }

} // namespace

KusanagiMediaPopup& KusanagiMediaPopup::instance() {
  static KusanagiMediaPopup popup;
  return popup;
}

void KusanagiMediaPopup::initialize(
    WaylandConnection& wayland, RenderContext* renderContext, MprisService* mpris, HttpClient* http,
    ParentResolver resolver
) {
  m_wayland = &wayland;
  m_renderContext = renderContext;
  m_mpris = mpris;
  m_http = http;
  m_resolver = std::move(resolver);
}

void KusanagiMediaPopup::shutdown() {
  m_openTimer.stop();
  m_closeTimer.stop();
  m_unloadTimer.stop();
  destroyPopup();
  m_anchors.clear();
  m_anchor = nullptr;
  m_wayland = nullptr;
  m_renderContext = nullptr;
  m_mpris = nullptr;
}

void KusanagiMediaPopup::registerIpc(IpcService& ipc) {
  ipc.bind(kusanagi::cli::msg::mediaPopup, [this](const std::string& args) -> std::string {
    std::string a = args;
    while (!a.empty() && (a.back() == ' ' || a.back() == '\n')) a.pop_back();
    if (a.empty() || a == "toggle") {
      m_pinned = !m_pinned;
    } else if (a == "show") {
      m_pinned = true;
    } else if (a == "hide") {
      m_pinned = false;
    } else {
      return "error: media-popup takes toggle | show | hide\n";
    }
    if (m_pinned) m_enabled = true; // pinning by hand opens it even where hovering wouldn't
    evaluate();
    return m_pinned ? "pinned\n" : "unpinned\n";
  });
}

void KusanagiMediaPopup::addAnchor(InputArea* area) {
  if (area == nullptr || std::ranges::find(m_anchors, area) != m_anchors.end()) return;
  m_anchors.push_back(area);
  if (m_anchor == nullptr) m_anchor = area;
}

void KusanagiMediaPopup::removeAnchor(InputArea* area) {
  std::erase(m_anchors, area);
  if (m_anchor != area) return;
  // The popup's parent bar is going away (reload or output change), and an xdg_popup must go first.
  m_openTimer.stop();
  m_closeTimer.stop();
  m_unloadTimer.stop();
  destroyPopup();
  m_anchorHover = false;
  m_anchor = m_anchors.empty() ? nullptr : m_anchors.front();
}

void KusanagiMediaPopup::onAnchorHover(InputArea* area, bool hovered, bool popupEnabled) {
  if (area == nullptr) return;
  if (hovered) {
    if (area != m_anchor && m_surface != nullptr) {
      // Another media module: reopen under it.
      destroyPopup();
    }
    m_anchor = area;
    m_enabled = popupEnabled;
  }
  if (area == m_anchor) m_anchorHover = hovered;
  evaluate();
}

bool KusanagiMediaPopup::wanted() const {
  if (m_mpris == nullptr || m_anchor == nullptr || !m_mpris->activePlayer().has_value()) return false;
  if (!m_enabled && !m_pinned) return false;
  return m_anchorHover || m_popupHover || m_pinned;
}

void KusanagiMediaPopup::onMprisChanged() {
  if (m_mpris != nullptr && !m_mpris->activePlayer().has_value()) m_pinned = false;
  if (m_surface != nullptr) refresh();
  evaluate();
}

void KusanagiMediaPopup::evaluate() {
  if (wanted()) {
    m_closeTimer.stop();
    m_unloadTimer.stop();
    if (m_surface == nullptr) {
      if (!m_openTimer.active()) {
        if (m_pinned && !m_anchorHover) {
          DeferredCall::callLater([this]() {
            if (wanted() && m_surface == nullptr) open();
          });
        } else {
          m_openTimer.start(kOpenDelay, [this]() {
            if (wanted() && m_surface == nullptr) open();
          });
        }
      }
    } else if (m_closing) {
      m_closing = false;
      tween(m_yAnim, m_y, 12.0F, 260.0F, easeOutQuint);
      tween(m_opacityAnim, m_opacity, 1.0F, 180.0F, easeLinear);
    }
    return;
  }
  m_openTimer.stop();
  if (m_surface != nullptr && !m_closeTimer.active() && !m_closing) {
    m_closeTimer.start(kCloseDelay, [this]() { startClosing(); });
  }
}

void KusanagiMediaPopup::startClosing() {
  if (m_surface == nullptr) return;
  m_closing = true;
  tween(m_yAnim, m_y, 4.0F, 260.0F, easeOutQuint);
  tween(m_opacityAnim, m_opacity, 0.0F, 180.0F, easeLinear);
  m_unloadTimer.start(std::chrono::milliseconds(kusanagi::ms(200)), [this]() {
    if (!wanted()) destroyPopup();
  });
}

void KusanagiMediaPopup::open() {
  if (m_wayland == nullptr || m_renderContext == nullptr || m_anchor == nullptr || !m_resolver) return;
  const auto parent = m_resolver(m_anchor);
  if (!parent.has_value() || parent->first.layerSurface == nullptr) {
    kLog.debug("media popup: no bar surface for the module");
    return;
  }
  const auto& ctx = parent->first;
  m_edge = parent->second;

  float ax = 0.0F;
  float ay = 0.0F;
  Node::absolutePosition(m_anchor, ax, ay);
  const float aw = m_anchor->width();
  const float ah = m_anchor->height();
  // Anchor to the module grown 6 px toward the card, and open away from the bar.
  float rx = ax;
  float ry = ay;
  float rw = aw;
  float rh = ah;
  std::uint32_t anchor = XDG_POSITIONER_ANCHOR_BOTTOM;
  if (m_edge == "bottom") {
    ry -= 6.0F;
    rh += 6.0F;
    anchor = XDG_POSITIONER_ANCHOR_TOP;
  } else if (m_edge == "left") {
    rw += 6.0F;
    anchor = XDG_POSITIONER_ANCHOR_RIGHT;
  } else if (m_edge == "right") {
    rx -= 6.0F;
    rw += 6.0F;
    anchor = XDG_POSITIONER_ANCHOR_LEFT;
  } else {
    rh += 6.0F;
  }
  const std::uint32_t gravity = anchor == XDG_POSITIONER_ANCHOR_TOP ? XDG_POSITIONER_GRAVITY_TOP
      : anchor == XDG_POSITIONER_ANCHOR_RIGHT                       ? XDG_POSITIONER_GRAVITY_RIGHT
      : anchor == XDG_POSITIONER_ANCHOR_LEFT                        ? XDG_POSITIONER_GRAVITY_LEFT
                                                                    : XDG_POSITIONER_GRAVITY_BOTTOM;

  PopupSurfaceConfig config{
      .anchorX = static_cast<std::int32_t>(std::round(rx)),
      .anchorY = static_cast<std::int32_t>(std::round(ry)),
      .anchorWidth = std::max(1, static_cast<std::int32_t>(std::round(rw))),
      .anchorHeight = std::max(1, static_cast<std::int32_t>(std::round(rh))),
      .width = static_cast<std::uint32_t>(kWinW),
      .height = static_cast<std::uint32_t>(kWinH),
      .anchor = anchor,
      .gravity = gravity,
      .constraintAdjustment =
          XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y,
      .serial = 0,
      .grab = false,
  };

  m_surface = std::make_unique<PopupSurface>(*m_wayland);
  m_surface->setRenderContext(m_renderContext);
  m_surface->setAnimationManager(&m_animations);
  m_surface->setConfigureCallback([this](std::uint32_t, std::uint32_t) {
    if (m_surface != nullptr) m_surface->requestLayout();
  });
  m_surface->setPrepareFrameCallback([this](bool, bool) { prepareFrame(); });
  m_surface->setDismissedCallback([this]() {
    DeferredCall::callLater([this]() { destroyPopup(); });
  });
  if (!m_surface->initialize(ctx.layerSurface, ctx.output, config)) {
    kLog.warn("failed to create the media popup");
    m_surface.reset();
    return;
  }
  // Only the card takes the pointer.
  m_surface->setInputRegion({InputRect{
      static_cast<int>(kCardX), 4, static_cast<int>(kCardW), static_cast<int>(kCardH) + 8,
  }});
  m_closing = false;
  m_y = 4.0F;
  m_opacity = 0.0F;
  m_built = false;
  refresh();
  tween(m_yAnim, m_y, 12.0F, 260.0F, easeOutQuint);
  tween(m_opacityAnim, m_opacity, 1.0F, 180.0F, easeLinear);
  // MPRIS doesn't push the position, so re-read it every second while shown.
  m_tick.startRepeating(std::chrono::milliseconds(1000), [this]() { refresh(); });
}

void KusanagiMediaPopup::destroyPopup() {
  m_tick.stop();
  m_animations.cancelAll();
  m_yAnim = m_opacityAnim = m_fracAnim = 0;
  m_input.setSceneRoot(nullptr);
  if (m_surface != nullptr) {
    m_surface->setDismissedCallback(nullptr);
    m_surface->setSceneRoot(nullptr);
  }
  m_root.reset();
  m_surface.reset();
  m_card = nullptr;
  m_shadow = m_bg = m_artBg = m_track = m_fill = nullptr;
  m_artIcon = m_player = m_title = m_artist = m_pos = m_len = nullptr;
  m_art = nullptr;
  m_seek = nullptr;
  m_prev = m_play = m_next = Button{};
  m_pointerInside = false;
  m_popupHover = false;
  m_closing = false;
  m_built = false;
  m_artUrl.clear();
  m_lastText.clear();
  m_frac = m_fracTarget = 0.0F;
}

void KusanagiMediaPopup::refresh() {
  if (m_surface != nullptr) m_surface->requestUpdate();
}

void KusanagiMediaPopup::prepareFrame() {
  if (m_renderContext == nullptr || m_surface == nullptr) return;
  const auto width = m_surface->width();
  const auto height = m_surface->height();
  if (width == 0 || height == 0) return;
  m_renderContext->makeCurrent(m_surface->renderTarget());
  Renderer& renderer = m_surface->renderTarget().renderer();
  UiPhaseScope layoutPhase(UiPhase::Layout);
  if (m_root == nullptr) {
    buildScene(static_cast<float>(width), static_cast<float>(height));
  }

  const auto player = m_mpris != nullptr ? m_mpris->activePlayer() : std::nullopt;
  if (!player.has_value()) {
    applyMotion();
    return;
  }
  m_busName = player->busName;
  m_playing = player->playbackStatus == "Playing";
  m_canSeek = player->canSeek;
  m_lengthUs = player->lengthUs;
  const std::int64_t posUs = m_mpris->position(player->busName).value_or(player->positionUs);

  const std::string title = player->title.empty() ? "Unknown" : player->title;
  const std::string artist = mpris::joinArtists(player->artists);
  const std::string text = player->identity + "\x1f" + title + "\x1f" + artist + "\x1f" + fmt(posUs) + "\x1f"
      + fmt(player->lengthUs) + (m_playing ? "p" : "");
  if (text != m_lastText) {
    m_lastText = text;
    m_player->setText("<span letter_spacing=\"1536\">" + escapeMarkup(player->identity) + "</span>");
    m_player->measure(renderer);
    m_title->setText(title);
    m_title->measure(renderer);
    m_artist->setText(artist);
    m_artist->measure(renderer);
    float y = 14.0F;
    m_player->setPosition(kTextX, y);
    y += m_player->height() + 2.0F;
    m_title->setPosition(kTextX, y);
    y += m_title->height() + 2.0F;
    m_artist->setPosition(kTextX, y);

    m_pos->setText(fmt(posUs));
    m_pos->measure(renderer);
    m_pos->setPosition(kTextX, kProgressY + 14.0F);
    m_len->setText(fmt(player->lengthUs));
    m_len->measure(renderer);
    m_len->setPosition(kCardW - 14.0F - m_len->width(), kProgressY + 14.0F);

    m_play.icon->setText(utf8(m_playing ? 0xf03e4 : 0xf040a));
    m_play.icon->measure(renderer);
    for (Button* b : {&m_prev, &m_play, &m_next}) {
      b->icon->setPosition(
          std::round((b->size - b->icon->width()) / 2.0F), std::floor((b->size - b->icon->height()) / 2.0F)
      );
    }
  }
  if (m_seek != nullptr) m_seek->setEnabled(m_canSeek);

  // Remote cover art is downloaded in the background.
  const std::string url = mpris::effectiveArtUrl(*player);
  if (url != m_artUrl || (!url.empty() && !m_art->hasImage())) {
    const std::string path = mpris::resolveArtworkSource(
        m_http, m_pendingArt, url, [this]() { refresh(); }, m_alive
    );
    if (url.empty()) {
      m_art->clear(renderer);
      m_artUrl.clear();
    } else if (!path.empty() && m_art->setSourceFile(renderer, path, static_cast<int>(kArt * 2.0F), true, true)) {
      m_artUrl = url;
    }
  }
  m_art->setVisible(m_art->hasImage());
  m_artIcon->setVisible(!m_art->hasImage());

  // The progress bar eases linearly over 900 ms, so the once-a-second ticks look continuous.
  const float frac = player->lengthUs > 0
      ? std::clamp(static_cast<float>(posUs) / static_cast<float>(player->lengthUs), 0.0F, 1.0F)
      : 0.0F;
  if (!m_built) {
    m_frac = frac;
    m_fracTarget = frac;
    m_built = true;
  } else if (std::abs(frac - m_fracTarget) > 1.0e-4F) {
    m_fracTarget = frac;
    tween(m_fracAnim, m_frac, frac, 900.0F, easeLinear);
  }
  applyMotion();
}

void KusanagiMediaPopup::buildScene(float width, float height) {
  uiAssertNotRendering("KusanagiMediaPopup::buildScene");
  const float radius = std::max(8.0F, kusanagi::radius() - 2.0F);
  const float artRadius = std::max(6.0F, kusanagi::radius() - 8.0F);
  const float panelOpacity = static_cast<float>(kusanagi::opt<double>("panel", "opacity", 0.94));
  const std::string font = kusanagi::font();

  m_root = ui::node({});
  m_root->setSize(width, height);
  m_surface->setSceneRoot(m_root.get());

  auto card = ui::node({.out = &m_card});
  card->setSize(kCardW, kCardH);
  card->setOpacity(0.0F);

  if (kusanagi::shadows()) {
    auto shadow = ui::box({.out = &m_shadow});
    shadow->setStyle(RoundedRectStyle{
        .fill = rgba(0.0F, 0.0F, 0.0F, 0.4F),
        .softness = 13.0F,
        .outerShadow = true,
        .shadowCutoutOffsetY = 6.0F,
    });
    shadow->setRadius(radius);
    shadow->setSize(kCardW, kCardH);
    shadow->setPosition(0.0F, 6.0F);
    card->addChild(std::move(shadow));
  }

  // The card swallows the pointer, and hovering it keeps it open.
  auto area = ui::inputArea({.width = kCardW, .height = kCardH});
  area->addChild(ui::box({
      .out = &m_bg,
      .fill = role(ColorRole::Surface, panelOpacity),
      .border = kusanagi::surfaceBorder(),
      .borderWidth = kusanagi::surfaceBorderWidth(),
      .radius = radius,
      .width = kCardW,
      .height = kCardH,
  }));

  // Cover
  auto artBg = ui::box({
      .out = &m_artBg,
      .fill = role(ColorRole::OnSurface, 0.08F),
      .radius = artRadius,
      .width = kArt,
      .height = kArt,
  });
  artBg->setPosition(14.0F, 14.0F);
  area->addChild(std::move(artBg));
  area->addChild(ui::label({
      .out = &m_artIcon,
      .text = utf8(0xf075a),
      .fontSize = 30.0F,
      .fontFamily = std::string(kIconFont),
      .color = role(ColorRole::OnSurfaceVariant),
      .maxLines = 1,
      .baselineMode = LabelBaselineMode::FontLine,
  }));
  auto art = ui::image({.out = &m_art, .fit = ImageFit::Cover, .radius = artRadius, .width = kArt, .height = kArt});
  art->setPosition(14.0F, 14.0F);
  art->setVisible(false);
  area->addChild(std::move(art));

  // Player, title and artist
  area->addChild(ui::label({
      .out = &m_player,
      .fontSize = 9.0F,
      .fontWeight = FontWeight::Bold,
      .fontFamily = font,
      .color = role(ColorRole::Primary),
      .maxWidth = kTextW,
      .maxLines = 1,
      .ellipsize = TextEllipsize::End,
      .baselineMode = LabelBaselineMode::FontLine,
      .configure = [](Label& l) { l.setUseMarkup(true); },
  }));
  area->addChild(ui::label({
      .out = &m_title,
      .fontSize = 13.0F,
      .fontWeight = FontWeight::Bold,
      .fontFamily = font,
      .color = role(ColorRole::OnSurface),
      .maxWidth = kTextW,
      .maxLines = 1,
      .ellipsize = TextEllipsize::End,
      .baselineMode = LabelBaselineMode::FontLine,
  }));
  area->addChild(ui::label({
      .out = &m_artist,
      .fontSize = 11.0F,
      .fontFamily = font,
      .color = role(ColorRole::OnSurfaceVariant),
      .maxWidth = kTextW,
      .maxLines = 1,
      .ellipsize = TextEllipsize::End,
      .baselineMode = LabelBaselineMode::FontLine,
  }));

  // Progress: the seek strip is taller than the track so it is easier to hit.
  auto seek = ui::inputArea({
      .out = &m_seek,
      .acceptedButtons = InputArea::buttonMask({BTN_LEFT}),
      .cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
      .width = kTextW,
      .height = 14.0F,
  });
  seek->setPosition(kTextX, kProgressY);
  seek->addChild(ui::box({
      .out = &m_track,
      .fill = role(ColorRole::OnSurface, 0.12F),
      .radius = 2.0F,
      .width = kTextW,
      .height = 4.0F,
  }));
  m_track->setPosition(0.0F, 5.0F);
  seek->addChild(ui::box({.out = &m_fill, .fill = role(ColorRole::Primary), .radius = 2.0F, .height = 4.0F}));
  m_fill->setPosition(0.0F, 5.0F);
  seek->setOnClick([this](const InputArea::PointerData& p) {
    if (m_mpris == nullptr || !m_canSeek || m_lengthUs <= 0 || m_busName.empty()) return;
    const float f = std::clamp(p.localX / kTextW, 0.0F, 1.0F);
    (void)m_mpris->setPosition(m_busName, static_cast<std::int64_t>(static_cast<double>(m_lengthUs) * f));
    refresh();
  });
  area->addChild(std::move(seek));

  for (Label** l : {&m_pos, &m_len}) {
    area->addChild(ui::label({
        .out = l,
        .fontSize = 9.0F,
        .fontFamily = font,
        .color = role(ColorRole::OnSurfaceVariant),
        .maxLines = 1,
        .baselineMode = LabelBaselineMode::FontLine,
    }));
  }

  // Previous, play and next, centred under the progress bar
  m_prev.size = 30.0F;
  m_play.size = 34.0F;
  m_play.filled = true;
  m_next.size = 30.0F;
  const float rowW = 30.0F + 6.0F + 34.0F + 6.0F + 30.0F;
  float x = std::round(kTextX + kTextW / 2.0F - rowW / 2.0F);
  const float rowY = kCardH - 8.0F - 34.0F;
  const std::array<std::pair<Button*, char32_t>, 3> buttons{{{&m_prev, 0xf04ae}, {&m_play, 0xf040a}, {&m_next, 0xf04ad}}};
  for (const auto& [b, icon] : buttons) {
    auto btn = ui::inputArea({
        .out = &b->area,
        .acceptedButtons = InputArea::buttonMask({BTN_LEFT}),
        .cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
        .width = b->size,
        .height = b->size,
    });
    btn->setPosition(x, rowY);
    x += b->size + 6.0F;
    btn->addChild(ui::box({
        .out = &b->face,
        .fill = b->filled ? role(ColorRole::Primary) : role(ColorRole::OnSurface, 0.0F),
        .radius = b->size / 2.0F,
        .width = b->size,
        .height = b->size,
    }));
    btn->addChild(ui::label({
        .out = &b->icon,
        .text = utf8(icon),
        .fontSize = b->filled ? 17.0F : 16.0F,
        .fontFamily = std::string(kIconFont),
        .color = b->filled ? role(ColorRole::Surface) : role(ColorRole::OnSurfaceVariant),
        .maxLines = 1,
        .baselineMode = LabelBaselineMode::FontLine,
    }));
    Button* bp = b;
    if (!b->filled) {
      btn->setOnEnter([this, bp](const InputArea::PointerData&) {
        bp->face->setFill(role(ColorRole::OnSurface, 0.1F));
        bp->icon->setColor(role(ColorRole::OnSurface));
        if (m_surface != nullptr) m_surface->requestRedraw();
      });
      btn->setOnLeave([this, bp]() {
        bp->face->setFill(role(ColorRole::OnSurface, 0.0F));
        bp->icon->setColor(role(ColorRole::OnSurfaceVariant));
        if (m_surface != nullptr) m_surface->requestRedraw();
      });
    }
    btn->setOnClick([this, bp](const InputArea::PointerData&) {
      if (m_mpris == nullptr || m_busName.empty()) return;
      if (bp == &m_prev) {
        (void)m_mpris->previous(m_busName);
      } else if (bp == &m_next) {
        (void)m_mpris->next(m_busName);
      } else {
        (void)m_mpris->playPause(m_busName);
      }
      refresh();
    });
    area->addChild(std::move(btn));
  }

  card->addChild(std::move(area));
  m_root->addChild(std::move(card));

  // Measure the labels that never change.
  Renderer& renderer = m_surface->renderTarget().renderer();
  m_artIcon->measure(renderer);
  m_artIcon->setPosition(
      14.0F + std::round((kArt - m_artIcon->width()) / 2.0F), 14.0F + std::floor((kArt - m_artIcon->height()) / 2.0F)
  );
  for (Button* b : {&m_prev, &m_play, &m_next}) {
    b->icon->measure(renderer);
  }

  m_input.setSceneRoot(m_root.get());
  m_input.setCursorShapeCallback([this](std::uint32_t serial, std::uint32_t shape) {
    if (m_wayland != nullptr) m_wayland->setCursorShape(serial, shape);
  });
}

void KusanagiMediaPopup::tween(
    AnimationManager::Id& id, float& value, float to, float durationMs, float (*ease)(float),
    std::function<void()> done
) {
  if (id != 0) {
    m_animations.cancel(id);
    id = 0;
  }
  if (std::abs(value - to) < 1.0e-4F) {
    value = to;
    applyMotion();
    if (done) done();
    return;
  }
  const float from = value;
  auto token = std::make_shared<AnimationManager::Id>(0);
  id = m_animations.animate(
      0.0F, 1.0F, durationMs, Easing::Linear,
      [this, &value, from, to, ease](float t) {
        value = from + (to - from) * ease(t);
        applyMotion();
      },
      [&id, token, done = std::move(done)]() {
        if (id == *token) id = 0;
        if (done) done();
      }
  );
  *token = id;
}

void KusanagiMediaPopup::applyMotion() {
  if (m_card == nullptr || m_surface == nullptr) return;
  m_card->setPosition(kCardX, std::round(m_y));
  m_card->setOpacity(std::clamp(m_opacity, 0.0F, 1.0F));
  if (m_fill != nullptr) {
    const float w = kTextW * std::clamp(m_frac, 0.0F, 1.0F);
    m_fill->setSize(w, 4.0F);
    m_fill->setVisible(w > 0.5F);
  }
  m_surface->requestRedraw();
}

bool KusanagiMediaPopup::onPointerEvent(const PointerEvent& event) {
  if (m_surface == nullptr || m_root == nullptr) return false;
  const bool ours = event.surface != nullptr && event.surface == m_surface->wlSurface();
  const auto sx = static_cast<float>(event.sx);
  const auto sy = static_cast<float>(event.sy);
  bool consumed = false;
  switch (event.type) {
  case PointerEvent::Type::Enter:
    if (ours) {
      m_pointerInside = true;
      m_popupHover = true;
      m_input.pointerEnter(sx, sy, event.serial);
      evaluate();
      consumed = true;
    }
    break;
  case PointerEvent::Type::Leave:
    if (ours) {
      m_pointerInside = false;
      m_popupHover = false;
      m_input.pointerLeave();
      evaluate();
      consumed = true;
    }
    break;
  case PointerEvent::Type::Motion:
    if (m_pointerInside) {
      m_input.pointerMotion(sx, sy, 0);
      consumed = true;
    }
    break;
  case PointerEvent::Type::Button:
    if (m_pointerInside) {
      m_input.pointerButton(sx, sy, event.button, event.pressed, event.serial, event.time, event.touch);
      consumed = true;
    }
    break;
  case PointerEvent::Type::Axis:
    break;
  }
  if (m_surface != nullptr && m_root != nullptr && (m_root->paintDirty() || m_root->layoutDirty())) {
    m_surface->requestRedraw();
  }
  return consumed;
}
