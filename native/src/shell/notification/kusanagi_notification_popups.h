#pragma once

// Kusanagi's notification popups on the NotificationManager. It replaces the generic
// NotificationToast with the same entry points. One overlay surface on the focused output holds the cards
// while there are any, newest first; each counts down notifications.timeout (paused on hover, critical
// ones stay).

#include "core/timer_manager.h"
#include "notification/notification.h"
#include "render/animation/animation_manager.h"
#include "render/scene/input_dispatcher.h"
#include "system/icon_resolver.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

class CompositorPlatform;
class ConfigService;
class HttpClient;
class LayerSurface;
class NotificationManager;
class Node;
class RenderContext;
class Renderer;
class WaylandConnection;
enum class NotificationEvent;
struct KeyboardEvent;
struct PointerEvent;
struct wl_output;

namespace kusanagi {
  class NotificationCard;
}

class KusanagiNotificationPopups {
public:
  KusanagiNotificationPopups() = default;
  ~KusanagiNotificationPopups();

  KusanagiNotificationPopups(const KusanagiNotificationPopups&) = delete;
  KusanagiNotificationPopups& operator=(const KusanagiNotificationPopups&) = delete;

  void initialize(
      WaylandConnection& wayland, CompositorPlatform& platform, ConfigService* config,
      NotificationManager* notifications, RenderContext* renderContext, HttpClient* httpClient = nullptr
  );
  void onConfigReload();
  void onOutputChange();
  void hideDndSuppressed();
  void requestLayout();
  void requestRedraw();

  bool onPointerEvent(const PointerEvent& event);
  bool onKeyboardEvent(const KeyboardEvent& event);

private:
  struct Popup {
    Notification n;
    kusanagi::NotificationCard* card = nullptr;
    bool dirty = true;  // (re)build the card on the next layout
    bool fresh = true;  // play the drop-in once it has a card
    bool placed = false;
    bool leaving = false;
    bool closeOnLeave = true;
    CloseReason leaveReason = CloseReason::Dismissed;
    bool hovered = false;
    bool replyMode = false;
    bool replyFocused = false;
    float remaining = 1.0F;
    float y = 0.0F;  // resting place in the stack (animated)
    float dx = 0.0F; // drop-in offset
    float dy = 0.0F;
    AnimationManager::Id countdownAnim = 0; // the hairline countdown,
    Timer expiry;                           // or just a timer without a hairline
    std::chrono::steady_clock::time_point countdownStart;
    float countdownTotal = 0.0F;
    AnimationManager::Id moveAnim = 0;
    AnimationManager::Id arriveAnim = 0;
  };

  void onNotificationEvent(const Notification& n, NotificationEvent event);
  void add(const Notification& n);
  void update(Popup& p, const Notification& n);
  void leave(Popup& p, bool closeOnLeave, CloseReason reason);
  void finish(std::uint32_t id); // card gone: close if asked, drop the popup
  void drop(std::uint32_t id);   // remove at once (no animation, no close)
  void startCountdown(Popup& p);
  void stopCountdown(Popup& p);
  void syncCountdown(Popup& p);
  void activate(std::uint32_t id);
  void invokeAction(std::uint32_t id, const std::string& key);
  void reply(std::uint32_t id, const std::string& text);
  void refreshAges();

  void ensureSurface();
  void destroySurface();
  void prepareFrame(bool needsLayout);
  void buildCard(Popup& p, Renderer& renderer);
  void restack(bool animate);
  void applyPlacement(Popup& p) const;
  void updateRegions();
  void syncKeyboard();

  [[nodiscard]] Popup* find(std::uint32_t id);
  [[nodiscard]] bool atBottom() const;
  [[nodiscard]] float spacing() const;
  [[nodiscard]] wl_output* pickOutput() const;

  WaylandConnection* m_wayland = nullptr;
  CompositorPlatform* m_platform = nullptr;
  ConfigService* m_config = nullptr;
  NotificationManager* m_notifications = nullptr;
  RenderContext* m_renderContext = nullptr;
  int m_callbackToken = -1;

  std::vector<std::unique_ptr<Popup>> m_popups; // newest first

  // The surface, only while there are popups.
  std::unique_ptr<LayerSurface> m_surface;
  AnimationManager m_animations; // declared before m_root so the scene goes first
  std::unique_ptr<Node> m_root;
  InputDispatcher m_input;
  wl_output* m_output = nullptr;
  std::string m_position;
  float m_sceneScale = 0.0F;
  bool m_pointerInside = false;
  float m_pointerX = 0.0F;
  float m_pointerY = 0.0F;

  IconResolver m_iconResolver;
  Timer m_ageTimer;
};
