#pragma once

#include "render/animation/animation_manager.h"
#include "wayland/layer_surface.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ConfigService;
class CompositorPlatform;
class IpcService;
class Box;
class Label;
class Node;
class RenderContext;
class WaylandConnection;
struct WaylandOutput;
struct wl_output;
struct wl_surface;

enum class OsdKind : std::uint8_t {
  Volume,
  Microphone,
  Brightness,
  Wifi,
  Bluetooth,
  PowerProfile,
  Caffeine,
  NightLight,
  Dnd,
  LockKeys,
  KeyboardLayout,
  Media,
  Privacy,
  KeyboardBacklight,
  GameMode
};

struct OsdContent {
  OsdKind kind = OsdKind::Volume;
  std::string icon;
  std::string value;
  float progress = 0.0F;
  bool showProgress = true;
  bool overLimit = false;
  bool inactive = false;
};

// The game mode pill. Whether to announce it (gamemode.announce) is up to the caller.
[[nodiscard]] OsdContent gameModeOsdContent(bool active);

// One pill that springs in at osd.position (top, bottom, left or right), morphs its width between
// kinds while it is up, and is unmapped entirely when idle. osd.style picks "pill", "minimal" (a slim strip)
// or "box" (a square in the lower middle with a segmented level).
class OsdOverlay {
public:
  OsdOverlay();
  ~OsdOverlay();

  OsdOverlay(const OsdOverlay&) = delete;
  OsdOverlay& operator=(const OsdOverlay&) = delete;

  void initialize(
      WaylandConnection& wayland, CompositorPlatform& platform, ConfigService* config, RenderContext* renderContext
  );
  void registerIpc(IpcService& ipc);
  void onOutputChange();
  void onConfigReload();
  void requestLayout();
  void requestRedraw();

  void show(const OsdContent& content);
  // Shows the OSD regardless of the per-kind toggles, for the settings page and `osd preview <kind>`.
  void preview(const OsdContent& content);
  [[nodiscard]] bool isEnabled() const noexcept;

  // True while any instance is on screen or animating into view. Callers use this to correct
  // already-visible content in place without popping up a fresh OSD.
  [[nodiscard]] bool isVisible() const;

private:
  enum class Mode : std::uint8_t { Top, Bottom, Left, Right, Box };

  struct Instance {
    wl_output* output = nullptr;
    std::unique_ptr<LayerSurface> surface;
    // sceneRoot must be destroyed before `animations` — ~Node() calls cancelForOwner().
    AnimationManager animations;
    std::unique_ptr<Node> sceneRoot;
    float outputHeight = 0.0F; // logical; box mode centres the square at 68 % of it

    Node* pill = nullptr;
    Box* shadow = nullptr;
    Box* background = nullptr;
    Box* circle = nullptr; // pill style: the accent circle behind the icon
    Label* icon = nullptr;
    Label* label = nullptr;
    Label* value = nullptr;
    Box* track = nullptr;
    Box* fill = nullptr;
    std::vector<Box*> segments; // box style: the 16-segment level

    // Layout targets and the animated state.
    float targetWidth = 0.0F;
    float pillHeight = 0.0F;
    float trackLength = 0.0F;
    bool hasBar = false;
    float targetLevel = 0.0F;
    float width = 0.0F;
    float level = 0.0F;
    float slide = 18.0F;
    float scale = 0.88F;
    float opacity = 0.0F;
    AnimationManager::Id widthAnim = 0;
    AnimationManager::Id levelAnim = 0;
    AnimationManager::Id slideAnim = 0;
    AnimationManager::Id scaleAnim = 0;
    AnimationManager::Id opacityAnim = 0;
    AnimationManager::Id hideTimer = 0;
    bool showing = false;
    bool showPending = false;
  };

  [[nodiscard]] std::vector<std::string> osdMonitors() const;
  [[nodiscard]] bool shouldRenderOnOutput(const WaylandOutput& output) const;
  void showContent(const OsdContent& content);
  void ensureSurfaces();
  void destroySurfaces();
  void setEnabledOverride(bool enabled);
  void prepareFrame(Instance& inst, bool needsUpdate);
  void buildScene(Instance& inst, std::uint32_t width, std::uint32_t height);
  void updateInstanceContent(Instance& inst);
  void startShow(Instance& inst);
  void startHide(Instance& inst);
  void applyMotion(Instance& inst);
  void tween(
      Instance& inst, AnimationManager::Id& id, float& value, float to, float durationMs, float (*ease)(float, float),
      float overshoot, std::function<void()> done = {}
  );

  WaylandConnection* m_wayland = nullptr;
  CompositorPlatform* m_platform = nullptr;
  ConfigService* m_config = nullptr;
  RenderContext* m_renderContext = nullptr;
  OsdContent m_content;
  Mode m_mode = Mode::Top;
  bool m_minimal = false;
  bool m_showValue = true;
  std::string m_lastLayoutKey;
  std::vector<std::string> m_lastMonitorSelectors;
  bool m_followFocusedOutput = false;
  wl_output* m_targetOutput = nullptr;
  std::optional<bool> m_runtimeEnabledOverride;
  bool m_lastConfiguredEnabled = true;
  std::vector<std::unique_ptr<Instance>> m_instances;
};
