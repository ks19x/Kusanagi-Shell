#include "shell/notification/kusanagi_notification_popups.h"

#include "shell/kusanagi/game_mode.h"

#include "compositors/compositor_platform.h"
#include "config/config_service.h"
#include "config/config_types.h"
#include "core/deferred_call.h"
#include "core/log.h"
#include "core/ui_phase.h"
#include "notification/notification_manager.h"
#include "render/render_context.h"
#include "render/render_target.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/notification/kusanagi_notification_card.h"
#include "ui/builders.h"
#include "ui/controls/input.h"
#include "wayland/layer_surface.h"
#include "wayland/surface.h"
#include "wayland/wayland_connection.h"
#include "wayland/wayland_seat.h"

#include <algorithm>
#include <cmath>

namespace {

  constexpr Logger kLog("notification");

  constexpr std::uint32_t kWidth = 400;
  constexpr float kBottomInset = 12.0F;    // the stack sits 12 px above the window's lower edge
  constexpr float kArriveMs = 480.0F;
  constexpr float kArriveFadeMs = 220.0F;
  constexpr float kMoveMs = 380.0F;

  float outBack(float t, float s) {
    t -= 1.0F;
    return t * t * ((s + 1.0F) * t + s) + 1.0F;
  }

  float outQuint(float t) {
    const float f = 1.0F - t;
    return 1.0F - f * f * f * f * f;
  }

  std::string setting(const char* key, const char* fallback) {
    return kusanagi::opt<std::string>("notifications", key, fallback);
  }

  // The first bar's edge decides where popups open from.
  std::string barEdge() {
    const auto& s = kusanagi::settings();
    if (const auto bars = s.find("bars"); bars != s.end() && bars->is_array() && !bars->empty()) {
      const auto& b = bars->front();
      if (b.is_object() && b.contains("position") && b["position"].is_string()) return b["position"].get<std::string>();
    }
    return kusanagi::opt<std::string>("bar", "position", "top");
  }


} // namespace

KusanagiNotificationPopups::~KusanagiNotificationPopups() {
  if (m_notifications != nullptr && m_callbackToken >= 0) {
    m_notifications->removeEventCallback(m_callbackToken);
  }
  destroySurface();
}

void KusanagiNotificationPopups::initialize(
    WaylandConnection& wayland, CompositorPlatform& platform, ConfigService* config, NotificationManager* notifications,
    RenderContext* renderContext, HttpClient* /*httpClient*/
) {
  m_wayland = &wayland;
  m_platform = &platform;
  m_config = config;
  m_notifications = notifications;
  m_renderContext = renderContext;
  m_callbackToken = m_notifications->addEventCallback([this](const Notification& n, NotificationEvent event) {
    onNotificationEvent(n, event);
  });
  // The Inbox holds the live notifications. A popup timing out dismisses its notification, so only the
  // missed ones (DND, game mode, pushed out by newer popups) stay.
  m_notifications->setLiveHistory(true);
}

// Model

KusanagiNotificationPopups::Popup* KusanagiNotificationPopups::find(std::uint32_t id) {
  for (auto& p : m_popups) {
    if (p->n.id == id) return p.get();
  }
  return nullptr;
}

void KusanagiNotificationPopups::onNotificationEvent(const Notification& n, NotificationEvent event) {
  switch (event) {
  case NotificationEvent::Added: {
    // Quiet while DND or game mode (gamemode.dnd) is on, unless it's critical. It still lands in the history.
    const auto* gm = GameModeService::instance();
    const bool dnd = (m_notifications != nullptr && m_notifications->doNotDisturb())
        || (gm != nullptr && gm->active() && kusanagi::opt<bool>("gamemode", "dnd", true));
    if (dnd && n.dndPolicy == NotificationDndPolicy::Respect && n.urgency != Urgency::Critical) break;
    if (Popup* p = find(n.id); p != nullptr) {
      update(*p, n);
    } else {
      add(n);
    }
    break;
  }
  case NotificationEvent::Updated:
    if (Popup* p = find(n.id); p != nullptr && !p->leaving) update(*p, n);
    break;
  case NotificationEvent::Closed:
    // "Clear all" in the Inbox (or `notifs clear`) empties the popups at once.
    if (m_notifications != nullptr && m_notifications->clearingAll()) {
      if (Popup* p = find(n.id); p != nullptr) {
        p->leaving = true;
        drop(n.id);
      }
      break;
    }
    // Closed elsewhere (the app, the Inbox, an action): fly off.
    if (Popup* p = find(n.id); p != nullptr && !p->leaving) leave(*p, false, CloseReason::ClosedByCall);
    break;
  }
}

void KusanagiNotificationPopups::add(const Notification& n) {
  auto p = std::make_unique<Popup>();
  p->n = n;
  // The popup owns the timing (notifications.timeout, paused on hover).
  if (m_notifications != nullptr) m_notifications->pauseExpiry(n.id);
  m_popups.insert(m_popups.begin(), std::move(p));

  // Newest first, at most notifications.max; the oldest make room.
  const int max = std::max(1, kusanagi::opt<int>("notifications", "max", 5));
  int kept = 0;
  std::vector<std::uint32_t> overflow;
  for (const auto& q : m_popups) {
    if (q->leaving) continue;
    if (++kept > max) overflow.push_back(q->n.id);
  }
  for (const auto id : overflow) drop(id);

  if (!m_ageTimer.active()) {
    m_ageTimer.startRepeating(std::chrono::seconds(30), [this]() { refreshAges(); });
  }
  ensureSurface();
  requestLayout();
  kLog.debug("kusanagi popups: showing #{}", n.id);
}

void KusanagiNotificationPopups::update(Popup& p, const Notification& n) {
  p.n = n;
  p.dirty = true;
  p.remaining = 1.0F;
  if (m_notifications != nullptr) m_notifications->pauseExpiry(n.id);
  stopCountdown(p);
  requestLayout();
}

void KusanagiNotificationPopups::leave(Popup& p, bool closeOnLeave, CloseReason reason) {
  if (p.leaving) return;
  p.leaving = true;
  p.closeOnLeave = closeOnLeave;
  p.leaveReason = reason;
  stopCountdown(p);
  if (p.card != nullptr && !p.card->leaving()) {
    p.card->dismiss();
    requestRedraw();
  } else if (p.card == nullptr) {
    const std::uint32_t id = p.n.id;
    DeferredCall::callLater([this, id]() { finish(id); });
  }
}

void KusanagiNotificationPopups::finish(std::uint32_t id) {
  Popup* p = find(id);
  if (p == nullptr) return;
  const bool close = p->closeOnLeave;
  const CloseReason reason = p->leaveReason;
  drop(id);
  if (!close || m_notifications == nullptr) return;
  // A timed-out popup dismisses its notification, whatever timeout the app asked for, so it leaves the
  // Inbox too.
  (void)m_notifications->close(id, reason == CloseReason::Expired ? CloseReason::Dismissed : reason);
}

void KusanagiNotificationPopups::drop(std::uint32_t id) {
  const auto it = std::ranges::find_if(m_popups, [id](const auto& p) { return p->n.id == id; });
  if (it == m_popups.end()) return;
  Popup& p = **it;
  stopCountdown(p);
  m_animations.cancel(p.moveAnim);
  m_animations.cancel(p.arriveAnim);
  // Not closed by us (pushed out by newer popups): a history notification stays in the Inbox, and a
  // toast-only one gets the NotificationManager's own expiry back.
  if (!p.leaving && m_notifications != nullptr && p.n.timeout > 0 && !m_notifications->inHistory(id)) {
    m_notifications->resumeExpiry(id, std::max(1, static_cast<int>(static_cast<float>(p.n.timeout) * p.remaining)));
  }
  if (p.card != nullptr && m_root != nullptr) {
    if (Input* in = p.card->replyInput(); in != nullptr && m_input.focusedArea() == in->inputArea()) {
      m_input.setFocus(nullptr);
    }
    (void)m_root->removeChild(p.card);
  }
  m_popups.erase(it);
  if (m_popups.empty()) {
    m_ageTimer.stop();
    destroySurface();
    return;
  }
  restack(true);
  syncKeyboard();
  if (m_surface != nullptr) m_surface->requestRedraw();
}

// Countdown: notifications.timeout, paused while hovered; critical ones stay.

void KusanagiNotificationPopups::startCountdown(Popup& p) {
  if (p.countdownAnim != 0 || p.expiry.active() || p.leaving || p.card == nullptr || p.n.urgency == Urgency::Critical) {
    return;
  }
  const float timeout = static_cast<float>(std::max(500, kusanagi::opt<int>("notifications", "timeout", 5000)));
  const std::uint32_t id = p.n.id;
  const auto expire = [this, id]() {
    if (Popup* q = find(id); q != nullptr) {
      q->countdownAnim = 0;
      q->remaining = 0.0F;
      leave(*q, true, CloseReason::Expired);
    }
  };
  if (!kusanagi::opt<bool>("notifications", "progress", true)) {
    // No hairline to draw, so a plain timer with no frames.
    p.countdownTotal = timeout;
    p.countdownStart = std::chrono::steady_clock::now();
    p.expiry.start(std::chrono::milliseconds(std::max(1, static_cast<int>(timeout * p.remaining))), expire);
    return;
  }
  p.countdownAnim = m_animations.animateTimer(
      p.remaining, 0.0F, timeout * p.remaining, Easing::Linear,
      [this, id](float v) {
        if (Popup* q = find(id); q != nullptr) {
          q->remaining = v;
          if (q->card != nullptr) q->card->setRemaining(v);
        }
      },
      expire
  );
  requestRedraw(); // animations only tick on frames
}

void KusanagiNotificationPopups::stopCountdown(Popup& p) {
  if (p.countdownAnim != 0) m_animations.cancel(p.countdownAnim);
  p.countdownAnim = 0;
  if (p.expiry.active()) {
    const float elapsed =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - p.countdownStart).count();
    p.remaining = std::max(0.0F, p.remaining - elapsed / std::max(1.0F, p.countdownTotal));
    p.expiry.stop();
  }
}

void KusanagiNotificationPopups::syncCountdown(Popup& p) {
  if (p.hovered || p.replyFocused) {
    stopCountdown(p);
  } else {
    startCountdown(p);
  }
}

// Actions

void KusanagiNotificationPopups::activate(std::uint32_t id) {
  Popup* p = find(id);
  if (p == nullptr || p->leaving || m_notifications == nullptr) return;
  // The default action if the app offered one, then close it.
  bool hasDefault = false;
  for (std::size_t i = 0; i + 1 < p->n.actions.size(); i += 2) {
    hasDefault = hasDefault || p->n.actions[i] == "default";
  }
  const std::string token = m_surface != nullptr ? m_wayland->requestActivationToken(m_surface->wlSurface()) : "";
  p->leaving = true; // the Closed event that follows must not fly it off, it's gone already
  drop(id);
  if (!hasDefault || !m_notifications->invokeAction(id, "default", token, true)) {
    (void)m_notifications->close(id, CloseReason::Dismissed);
  }
}

void KusanagiNotificationPopups::invokeAction(std::uint32_t id, const std::string& key) {
  Popup* p = find(id);
  if (p == nullptr || p->leaving || m_notifications == nullptr) return;
  const std::string token = m_surface != nullptr ? m_wayland->requestActivationToken(m_surface->wlSurface()) : "";
  if (!m_notifications->invokeAction(id, key, token, true)) {
    kLog.warn("kusanagi popups: failed to invoke action '{}' for #{}", key, id);
  }
  if (Popup* q = find(id); q != nullptr) leave(*q, true, CloseReason::Dismissed);
}

void KusanagiNotificationPopups::reply(std::uint32_t id, const std::string& text) {
  if (find(id) == nullptr || m_notifications == nullptr) return;
  const std::string token = m_surface != nullptr ? m_wayland->requestActivationToken(m_surface->wlSurface()) : "";
  if (!m_notifications->invokeInlineReply(id, text, token, true)) {
    kLog.warn("kusanagi popups: failed to send the inline reply for #{}", id);
  }
  if (Popup* q = find(id); q != nullptr) {
    q->replyFocused = false;
    leave(*q, true, CloseReason::Dismissed);
  }
}

void KusanagiNotificationPopups::refreshAges() {
  for (auto& p : m_popups) {
    if (p->card != nullptr) p->card->setAge(kusanagi::notificationAge(p->n));
  }
  if (m_surface != nullptr) m_surface->requestLayout();
}

// Engine hooks

void KusanagiNotificationPopups::onConfigReload() {
  if (m_popups.empty()) return;
  // A new position or bar edge needs a new surface; a new style needs new cards (without a second drop-in).
  if (m_surface != nullptr && (setting("position", "top-right") != m_position)) {
    destroySurface();
  }
  for (auto& p : m_popups) p->dirty = true;
  ensureSurface();
  requestLayout();
}

void KusanagiNotificationPopups::onOutputChange() {
  if (m_surface == nullptr || m_wayland == nullptr) return;
  const auto* out = m_wayland->findOutputByWl(m_output);
  if (out == nullptr || !out->done || !out->hasUsableGeometry()) {
    destroySurface();
    if (!m_popups.empty()) {
      for (auto& p : m_popups) p->dirty = true;
      ensureSurface();
      requestLayout();
    }
  }
}

void KusanagiNotificationPopups::hideDndSuppressed() {
  // Do not disturb only keeps new popups quiet; the ones on screen finish their countdown.
}

void KusanagiNotificationPopups::requestLayout() {
  if (m_surface != nullptr) m_surface->requestLayout();
}

void KusanagiNotificationPopups::requestRedraw() {
  if (m_surface != nullptr) m_surface->requestRedraw();
}

// Surface

bool KusanagiNotificationPopups::atBottom() const { return m_position.starts_with("bottom"); }

float KusanagiNotificationPopups::spacing() const {
  const std::string style = setting("style", "comfortable");
  return style == "compact" || style == "minimal" ? 6.0F : 10.0F;
}

wl_output* KusanagiNotificationPopups::pickOutput() const {
  const auto usable = [](const WaylandOutput& o) { return o.done && o.output != nullptr && o.hasUsableGeometry(); };
  if (m_config != nullptr) {
    for (const auto& match : m_config->config().notification.monitors) {
      for (const auto& o : m_wayland->outputs()) {
        if (usable(o) && outputMatchesSelector(match, o)) return o.output;
      }
    }
  }
  if (m_platform != nullptr) {
    if (wl_output* focused = m_platform->preferredInteractiveOutput(); focused != nullptr) return focused;
  }
  for (const auto& o : m_wayland->outputs()) {
    if (usable(o)) return o.output;
  }
  return nullptr;
}

void KusanagiNotificationPopups::ensureSurface() {
  if (m_surface != nullptr || m_wayland == nullptr || m_renderContext == nullptr) return;
  wl_output* output = pickOutput();
  if (output == nullptr) return;

  // top-right, top-center, top-left, bottom-right, bottom-center or bottom-left
  m_position = setting("position", "top-right");
  const bool right = m_position.ends_with("right");
  const bool left = m_position.ends_with("left");
  const bool barBottom = barEdge() == "bottom";
  std::uint32_t anchor = LayerShellAnchor::Top | LayerShellAnchor::Bottom;
  if (right) anchor |= LayerShellAnchor::Right;
  if (left) anchor |= LayerShellAnchor::Left;

  m_surface = std::make_unique<LayerSurface>(
      *m_wayland,
      LayerSurfaceConfig{
          .nameSpace = "kusanagi-notification",
          .layer = LayerShellLayer::Overlay,
          .anchor = anchor,
          .width = kWidth,
          .height = 0,
          .exclusiveZone = 0,
          .marginTop = barBottom ? 10 : 6,
          .marginRight = right ? 10 : 0,
          .marginBottom = barBottom ? 6 : 10,
          .marginLeft = left ? 10 : 0,
          .keyboard = LayerShellKeyboard::None,
          .defaultWidth = kWidth,
          .defaultHeight = 600,
          .prewarmBlur = true,
      }
  );
  m_surface->setRenderContext(m_renderContext);
  m_surface->setConfigureCallback([this](std::uint32_t, std::uint32_t) { m_surface->requestLayout(); });
  m_surface->setPrepareFrameCallback([this](bool, bool needsLayout) { prepareFrame(needsLayout); });
  m_surface->setFrameTickCallback([this](float) { updateRegions(); });
  m_surface->setAnimationManager(&m_animations);
  if (!m_surface->initialize(output)) {
    kLog.warn("kusanagi popups: failed to create the surface");
    m_surface.reset();
    return;
  }
  m_output = output;
}

void KusanagiNotificationPopups::destroySurface() {
  m_animations.cancelAll();
  for (auto& p : m_popups) {
    p->card = nullptr;
    p->countdownAnim = 0;
    p->moveAnim = 0;
    p->arriveAnim = 0;
    p->placed = false;
    p->dirty = true;
  }
  m_input.setSceneRoot(nullptr);
  if (m_surface != nullptr) {
    m_surface->clearBlurRegion();
    m_surface->setSceneRoot(nullptr);
  }
  m_root.reset();
  m_surface.reset();
  m_output = nullptr;
  m_pointerInside = false;
  m_sceneScale = 0.0F;
}

void KusanagiNotificationPopups::prepareFrame(bool needsLayout) {
  if (m_surface == nullptr || m_renderContext == nullptr) return;
  const auto width = m_surface->width();
  const auto height = m_surface->height();
  if (width == 0 || height == 0) return;
  if (!m_renderContext->makeCurrent(m_surface->renderTarget())) return;
  Renderer& renderer = m_surface->renderTarget().renderer();

  UiPhaseScope layoutPhase(UiPhase::Layout);
  const bool newScene = m_root == nullptr
      || std::abs(m_sceneScale - renderer.renderScale()) > 0.0001F
      || static_cast<std::uint32_t>(std::round(m_root->width())) != width
      || static_cast<std::uint32_t>(std::round(m_root->height())) != height;
  if (newScene) {
    m_sceneScale = renderer.renderScale();
    for (auto& p : m_popups) {
      m_animations.cancel(p->moveAnim);
      m_animations.cancel(p->arriveAnim);
      p->moveAnim = 0;
      p->arriveAnim = 0;
      p->card = nullptr;
      p->dirty = true;
    }
    m_input.setSceneRoot(nullptr);
    m_root = ui::node({
        .width = static_cast<float>(width),
        .height = static_cast<float>(height),
        .configure = [this](Node& node) { node.setAnimationManager(&m_animations); },
    });
    m_input.setSceneRoot(m_root.get());
    m_input.setTextInputContext(m_surface->wlSurface(), m_wayland->textInputService());
    m_input.setCursorShapeCallback([this](std::uint32_t serial, std::uint32_t shape) {
      m_wayland->setCursorShape(serial, shape);
    });
    m_surface->setSceneRoot(m_root.get());
  }

  bool rebuilt = false;
  for (auto& p : m_popups) {
    if (p->dirty && !p->leaving) {
      buildCard(*p, renderer);
      rebuilt = true;
    }
  }
  if (rebuilt || newScene) restack(!newScene);
  if (needsLayout && !rebuilt) m_root->layout(renderer);

  updateRegions();
  syncKeyboard();
  if (rebuilt && m_pointerInside) m_input.pointerMotion(m_pointerX, m_pointerY, 0);
  m_surface->requestRedraw();
}

void KusanagiNotificationPopups::buildCard(Popup& p, Renderer& renderer) {
  uiAssertNotRendering("KusanagiNotificationPopups::buildCard");
  const std::uint32_t id = p.n.id;
  kusanagi::NotificationCardData data = kusanagi::notificationCardData(p.n, m_iconResolver);
  data.replyMode = p.replyMode;
  kusanagi::NotificationCardCallbacks callbacks{
      .onActivate = [this, id]() { DeferredCall::callLater([this, id]() { activate(id); }); },
      .onDismissed =
          [this, id]() {
            if (Popup* q = find(id); q != nullptr) {
              if (!q->leaving) {
                q->leaving = true;
                q->closeOnLeave = true;
                q->leaveReason = CloseReason::Dismissed;
              }
              finish(id);
            }
          },
      .onAction = [this, id](const std::string& key) { DeferredCall::callLater([this, id, key]() { invokeAction(id, key); }); },
      .onReplyRequested =
          [this, id]() {
            if (Popup* q = find(id); q != nullptr && !q->replyMode) {
              q->replyMode = true;
              q->dirty = true;
              requestLayout();
            }
          },
      .onReply = [this, id](const std::string& text) { DeferredCall::callLater([this, id, text]() { reply(id, text); }); },
      .onReplyFocusChanged =
          [this, id](bool focused) {
            if (Popup* q = find(id); q != nullptr) {
              q->replyFocused = focused;
              syncCountdown(*q);
            }
          },
      .onHoverChanged =
          [this, id](bool hovered) {
            if (Popup* q = find(id); q != nullptr) {
              q->hovered = hovered;
              syncCountdown(*q);
            }
          },
  };

  const bool had = p.card != nullptr;
  if (had) {
    stopCountdown(p);
    (void)m_root->removeChild(p.card);
    p.card = nullptr;
    // The new card reports hover and reply focus afresh (pointer re-sync after the build).
    p.hovered = false;
    p.replyFocused = false;
    // A drop-in cut short by the rebuild settles where it was heading.
    m_animations.cancel(p.arriveAnim);
    p.arriveAnim = 0;
    p.dx = 0.0F;
    p.dy = 0.0F;
  }
  auto card = std::make_unique<kusanagi::NotificationCard>(
      renderer, &m_animations, std::move(data), kusanagi::NotificationCardStyle::fromSettings(true), std::move(callbacks)
  );
  card->setRemaining(p.remaining);
  p.card = static_cast<kusanagi::NotificationCard*>(m_root->addChild(std::move(card)));
  applyPlacement(p);
  p.dirty = false;
  if (p.replyMode && p.card->replyInput() != nullptr) {
    m_input.setFocus(p.card->replyInput()->inputArea());
  }
  if (!p.hovered && !p.replyFocused) startCountdown(p);
}

// Positions in the column (newest first): from the top edge, or ending 12 px above the bottom one.
void KusanagiNotificationPopups::restack(bool animate) {
  if (m_root == nullptr) return;
  const float gap = spacing();
  float column = 0.0F;
  int n = 0;
  for (const auto& p : m_popups) {
    if (p->card == nullptr) continue;
    column += p->card->cardHeight();
    ++n;
  }
  column += gap * static_cast<float>(std::max(0, n - 1));
  float y = atBottom() ? std::round(m_root->height() - kBottomInset - column) : 0.0F;

  const bool center = m_position.ends_with("center");
  const float fromX = m_position.ends_with("left") ? -60.0F : center ? 0.0F : 60.0F;
  const float fromY = center ? (atBottom() ? 24.0F : -24.0F) : 0.0F;

  for (auto& pp : m_popups) {
    Popup& p = *pp;
    if (p.card == nullptr) continue;
    const float target = y;
    y += p.card->cardHeight() + gap;
    const std::uint32_t id = p.n.id;

    if (!p.placed || !animate) {
      p.y = target;
      p.placed = true;
    } else if (std::abs(p.y - target) > 0.5F) {
      // The others glide to their new place when one arrives or leaves.
      m_animations.cancel(p.moveAnim);
      const float from = p.y;
      p.moveAnim = m_animations.animate(
          0.0F, 1.0F, kMoveMs, Easing::Linear,
          [this, id, from, target](float t) {
            if (Popup* q = find(id); q != nullptr) {
              q->y = from + (target - from) * outQuint(t);
              applyPlacement(*q);
            }
          },
          [this, id]() {
            if (Popup* q = find(id); q != nullptr) q->moveAnim = 0;
          },
          p.card
      );
    }

    if (p.fresh) {
      // Arrival: drop in from the edge the stack lives on.
      p.fresh = false;
      p.dx = fromX;
      p.dy = fromY;
      p.card->setOpacity(0.0F);
      p.card->setScale(0.94F);
      const float slide = kusanagi::bounce(1.1F);
      const float grow = kusanagi::bounce(1.4F);
      p.arriveAnim = m_animations.animate(
          0.0F, 1.0F, kArriveMs, Easing::Linear,
          [this, id, fromX, fromY, slide, grow](float t) {
            Popup* q = find(id);
            if (q == nullptr || q->card == nullptr) return;
            const float s = outBack(t, slide);
            q->dx = fromX * (1.0F - s);
            q->dy = fromY * (1.0F - s);
            q->card->setOpacity(std::min(1.0F, t * kArriveMs / kArriveFadeMs));
            q->card->setScale(0.94F + 0.06F * outBack(t, grow));
            applyPlacement(*q);
          },
          [this, id]() {
            if (Popup* q = find(id); q != nullptr) q->arriveAnim = 0;
          },
          p.card
      );
    }
    applyPlacement(p);
  }
}

void KusanagiNotificationPopups::applyPlacement(Popup& p) const {
  if (p.card != nullptr) p.card->setPosition(p.dx, p.y + p.dy);
}

void KusanagiNotificationPopups::updateRegions() {
  if (m_surface == nullptr) return;
  std::vector<InputRect> input;
  std::vector<InputRect> blur;
  const float r = std::max(8.0F, kusanagi::radius() - 2.0F);
  const bool minimal = setting("style", "comfortable") == "minimal";
  for (const auto& p : m_popups) {
    if (p->card == nullptr) continue;
    const int x = static_cast<int>(std::floor(p->card->x()));
    const int y = static_cast<int>(std::floor(p->card->y()));
    const int w = static_cast<int>(std::ceil(p->card->width()));
    const int h = static_cast<int>(std::ceil(p->card->cardHeight()));
    input.push_back({x, y, w, h});
    // Only cards at rest get blur; a blur rect lagging behind a moving card would show.
    if (!p->leaving && p->arriveAnim == 0 && p->moveAnim == 0 && !p->card->hovered()) {
      const auto strips = Surface::tessellateRoundedRect(x, y, w, h, minimal ? static_cast<float>(h) / 2.0F : r);
      blur.insert(blur.end(), strips.begin(), strips.end());
    }
  }
  m_surface->setInputRegion(input);
  if (blur.empty()) {
    m_surface->clearBlurRegion();
  } else {
    m_surface->setBlurRegion(blur);
  }
}

void KusanagiNotificationPopups::syncKeyboard() {
  if (m_surface == nullptr) return;
  const bool wants = std::ranges::any_of(m_popups, [](const auto& p) { return p->replyMode && !p->leaving; });
  m_surface->setKeyboardInteractivity(wants ? LayerShellKeyboard::OnDemand : LayerShellKeyboard::None);
}

// Input

bool KusanagiNotificationPopups::onPointerEvent(const PointerEvent& event) {
  if (m_surface == nullptr || m_root == nullptr) return false;
  const bool ours = event.surface == m_surface->wlSurface();
  const auto sx = static_cast<float>(event.sx);
  const auto sy = static_cast<float>(event.sy);
  bool consumed = false;

  switch (event.type) {
  case PointerEvent::Type::Enter:
    if (ours) {
      m_pointerInside = true;
      m_pointerX = sx;
      m_pointerY = sy;
      m_input.pointerEnter(sx, sy, event.serial);
    }
    break;
  case PointerEvent::Type::Leave:
    if (ours) {
      m_pointerInside = false;
      m_input.pointerLeave();
    }
    break;
  case PointerEvent::Type::Motion:
    if (m_pointerInside) {
      m_pointerX = sx;
      m_pointerY = sy;
      m_input.pointerMotion(sx, sy, 0);
    }
    break;
  case PointerEvent::Type::Button:
    if (!ours && event.pressed) {
      // A click elsewhere ends typing a reply.
      if (m_input.focusedArea() != nullptr) m_input.setFocus(nullptr);
    }
    if (m_pointerInside) {
      m_pointerX = sx;
      m_pointerY = sy;
      m_input.pointerMotion(sx, sy, event.serial);
      m_input.pointerButton(sx, sy, event.button, event.pressed, event.serial, event.time, event.touch);
      consumed = true;
    }
    break;
  case PointerEvent::Type::Axis:
    break;
  }

  // Hover changes on controls can mark layout dirt, but the cards don't change geometry on hover.
  if (m_root != nullptr && (m_root->paintDirty() || m_root->layoutDirty())) m_surface->requestRedraw();
  return consumed;
}

bool KusanagiNotificationPopups::onKeyboardEvent(const KeyboardEvent& event) {
  if (m_surface == nullptr || m_wayland == nullptr || m_wayland->lastKeyboardSurface() != m_surface->wlSurface()) {
    return false;
  }
  if (m_input.focusedArea() == nullptr) return false;
  m_input.keyEvent(event.sym, event.utf32, event.modifiers, event.pressed, event.preedit);
  if (m_root != nullptr && (m_root->paintDirty() || m_root->layoutDirty())) m_surface->requestLayout();
  return true;
}
