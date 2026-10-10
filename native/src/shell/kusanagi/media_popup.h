#pragma once

// Media card that drops out of the bar when a "media" module is hovered: cover, player, title, artist, a
// seekable progress bar and transport buttons. It stays while the pointer is on the module or the card, and
// the `media-popup` IPC target pins it open. Modules turn it off with "popup": false (bar.mediaPopup on the
// classic bar). The popup surface only exists while the card is shown.

#include "core/timer_manager.h"
#include "render/animation/animation_manager.h"
#include "render/scene/input_dispatcher.h"
#include "ui/dialogs/layer_popup_host.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class Box;
class HttpClient;
class Image;
class InputArea;
class IpcService;
class Label;
class MprisService;
class Node;
class PopupSurface;
class RenderContext;
class WaylandConnection;
struct PointerEvent;

class KusanagiMediaPopup {
public:
  // The bar surface a node lives in (the popup's parent), and that bar's edge.
  using ParentResolver =
      std::function<std::optional<std::pair<LayerPopupParentContext, std::string>>(const Node* node)>;

  static KusanagiMediaPopup& instance();

  void initialize(
      WaylandConnection& wayland, RenderContext* renderContext, MprisService* mpris, HttpClient* http,
      ParentResolver resolver
  );
  void registerIpc(IpcService& ipc);
  void shutdown();

  // Called by the bar media modules.
  void addAnchor(InputArea* area);
  void removeAnchor(InputArea* area);
  void onAnchorHover(InputArea* area, bool hovered, bool popupEnabled);

  [[nodiscard]] bool onPointerEvent(const PointerEvent& event);
  void onMprisChanged();

private:
  KusanagiMediaPopup() = default;

  struct Button {
    InputArea* area = nullptr;
    Box* face = nullptr;
    Label* icon = nullptr;
    float size = 30.0F;
    bool filled = false;
  };

  void evaluate();
  void open();
  void startClosing();
  void destroyPopup();
  void prepareFrame();
  void buildScene(float width, float height);
  void refresh();
  void applyMotion();
  void tween(
      AnimationManager::Id& id, float& value, float to, float durationMs, float (*ease)(float),
      std::function<void()> done = {}
  );
  [[nodiscard]] bool wanted() const;

  WaylandConnection* m_wayland = nullptr;
  RenderContext* m_renderContext = nullptr;
  MprisService* m_mpris = nullptr;
  HttpClient* m_http = nullptr;
  ParentResolver m_resolver;

  std::vector<InputArea*> m_anchors;
  InputArea* m_anchor = nullptr; // the media module hovered last, or the first one
  bool m_anchorHover = false;
  bool m_popupHover = false;
  bool m_pinned = false;
  bool m_enabled = true;
  bool m_closing = false;
  std::string m_edge = "top";
  Timer m_openTimer;
  Timer m_closeTimer;
  Timer m_unloadTimer;
  Timer m_tick;

  std::unique_ptr<PopupSurface> m_surface;
  // m_root must be destroyed before m_animations, since ~Node cancels its animations.
  AnimationManager m_animations;
  std::unique_ptr<Node> m_root;
  InputDispatcher m_input;
  bool m_pointerInside = false;
  bool m_built = false;

  Node* m_card = nullptr;
  Box* m_shadow = nullptr;
  Box* m_bg = nullptr;
  Box* m_artBg = nullptr;
  Label* m_artIcon = nullptr;
  Image* m_art = nullptr;
  Label* m_player = nullptr;
  Label* m_title = nullptr;
  Label* m_artist = nullptr;
  Box* m_track = nullptr;
  Box* m_fill = nullptr;
  InputArea* m_seek = nullptr;
  Label* m_pos = nullptr;
  Label* m_len = nullptr;
  Button m_prev;
  Button m_play;
  Button m_next;

  std::string m_busName;
  std::string m_artUrl;
  std::string m_lastText;
  std::int64_t m_lengthUs = 0;
  bool m_canSeek = false;
  bool m_playing = false;
  std::unordered_set<std::string> m_pendingArt;
  std::shared_ptr<int> m_alive = std::make_shared<int>(0);

  float m_y = 4.0F;
  float m_opacity = 0.0F;
  float m_frac = 0.0F;
  float m_fracTarget = 0.0F;
  AnimationManager::Id m_yAnim = 0;
  AnimationManager::Id m_opacityAnim = 0;
  AnimationManager::Id m_fracAnim = 0;
};
