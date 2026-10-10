#pragma once

// Screenshot card: after a screenshot, a preview in a corner (screenshot.position) with copy, edit, folder
// and delete actions. Click the picture to open it, swipe it toward the screen edge to dismiss it, and hover
// to pause the countdown. The screenshot scripts call it with `screenshot-notify <file>`. The layer surface
// and the preview texture only exist while the card is shown.

#include "render/animation/animation_manager.h"
#include "render/scene/input_dispatcher.h"
#include "wayland/layer_surface.h"

#include <array>
#include <memory>
#include <string>

class AsyncTextureCache;
class Box;
class CompositorPlatform;
class Image;
class InputArea;
class IpcService;
class Label;
class Node;
class RenderContext;
class WaylandConnection;
struct PointerEvent;
struct wl_output;

class KusanagiScreenshotCard {
public:
  KusanagiScreenshotCard();
  ~KusanagiScreenshotCard();

  KusanagiScreenshotCard(const KusanagiScreenshotCard&) = delete;
  KusanagiScreenshotCard& operator=(const KusanagiScreenshotCard&) = delete;

  void initialize(
      WaylandConnection& wayland, CompositorPlatform& platform, RenderContext* renderContext,
      AsyncTextureCache* textures
  );
  void registerIpc(IpcService& ipc);
  void onOutputChange();

  void show(const std::string& file);
  void hide();
  [[nodiscard]] bool onPointerEvent(const PointerEvent& event);

private:
  struct Chip {
    InputArea* area = nullptr;
    Box* face = nullptr;
    Label* icon = nullptr;
    Label* label = nullptr;
  };

  void ensureSurface();
  void destroySurface();
  void prepareFrame();
  void buildScene(float width, float height);
  void startShow();
  void applyMotion();
  void startCountdown();
  void pauseCountdown();
  void act(int action);
  void flyOff();
  void tween(
      AnimationManager::Id& id, float& value, float to, float durationMs, float (*ease)(float, float), float overshoot,
      std::function<void()> done = {}
  );

  WaylandConnection* m_wayland = nullptr;
  CompositorPlatform* m_platform = nullptr;
  RenderContext* m_renderContext = nullptr;
  AsyncTextureCache* m_textures = nullptr;

  std::string m_file;
  bool m_onRight = true;
  bool m_onBottom = true;
  wl_output* m_output = nullptr;

  std::unique_ptr<LayerSurface> m_surface;
  // m_root must be destroyed before m_animations, since ~Node cancels its animations.
  AnimationManager m_animations;
  std::unique_ptr<Node> m_root;
  InputDispatcher m_input;
  bool m_pointerInside = false;
  bool m_showPending = false;

  Node* m_card = nullptr;
  Box* m_shadow = nullptr;
  InputArea* m_cardArea = nullptr;
  Box* m_bg = nullptr;
  Box* m_shotBg = nullptr;
  Image* m_shot = nullptr;
  Label* m_name = nullptr;
  Box* m_hairline = nullptr;
  std::array<Chip, 4> m_chips{};

  // Motion and the countdown
  bool m_showing = false;
  float m_opacity = 0.0F;
  float m_scale = 0.86F;
  float m_swipe = 0.0F;
  float m_remaining = 1.0F;
  AnimationManager::Id m_opacityAnim = 0;
  AnimationManager::Id m_scaleAnim = 0;
  AnimationManager::Id m_swipeAnim = 0;
  AnimationManager::Id m_countdown = 0;
  float m_pressX = 0.0F;
  bool m_dragging = false;
  bool m_dragged = false;
};
