#pragma once

// One Kusanagi bar module. config/kusanagi_import.cpp turns each module in settings.json "bars" into a
// widget of type "kusanagi" whose `spec` holds the module's effective JSON. This widget reads its data from
// the shell's services, applies `when` state overrides and renders the format string as Pango markup.

#include "core/timer_manager.h"
#include "shell/bar/widget.h"
#include "shell/bar/widgets/kusanagi_box.h"
#include "wayland/wayland_toplevels.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class Image;
class KusanagiTaskbar;
class Label;
class InputArea;
class PipeWireService;
class SystemMonitorService;
class UPowerService;
class MprisService;
class NotificationManager;
class IdleInhibitor;
class BrightnessService;
class BluetoothService;
class WeatherService;
class CompositorPlatform;

struct KusanagiModuleServices {
  PipeWireService* audio = nullptr;
  SystemMonitorService* sysmon = nullptr;
  UPowerService* upower = nullptr;
  MprisService* mpris = nullptr;
  NotificationManager* notifications = nullptr;
  IdleInhibitor* idle = nullptr;
  BrightnessService* brightness = nullptr;
  BluetoothService* bluetooth = nullptr;
  WeatherService* weather = nullptr;
  CompositorPlatform* platform = nullptr;
};

class KusanagiModuleWidget : public Widget {
public:
  KusanagiModuleWidget(KusanagiModuleServices services, std::string specJson);
  ~KusanagiModuleWidget() override;

  void create() override;
  [[nodiscard]] bool wantsSecondTick() const override;
  [[nodiscard]] bool isKusanagiClock() const override { return m_type == "clock"; }
  // The bare group's inset across the bar (edge side, inner side), scaled. Used for the clock's island.
  [[nodiscard]] std::pair<float, float> groupInset() const { return {m_groupEdge * m_contentScale, m_groupInner * m_contentScale}; }

  // Resolves a Kusanagi colour token (accent, text, dim, bg/0.7, #rrggbb[aa], transparent, ...).
  [[nodiscard]] static ColorSpec tokenColor(const std::string& token, const ColorSpec& fallback);

private:
  struct Data {
    nlohmann::json vars = nlohmann::json::object();
    double level = -1.0;
    std::string status;         // space-separated states from the provider
    std::string format;         // provider default
    nlohmann::json icons;       // provider default (array by level, or object by state)
    std::string icon;           // provider's own {icon} when there are no icons
    nlohmann::json thresholds;  // provider default thresholds
    bool lowIsBad = false;
    bool shown = true;
    std::string fg;             // provider default colour token
    std::string tooltip;        // the provider's tooltip
    bool tooltipRich = false;   // the tooltip is already markup (the clock's calendar)
  };

  void doLayout(Renderer& renderer, float containerWidth, float containerHeight) override;
  void doUpdate(Renderer& renderer) override;
  // The active `when` state's own click, right or middle click, or scroll action, if it has one.
  [[nodiscard]] std::optional<kusanagi::bar::WidgetAction> gestureOverride(kusanagi::bar::Gesture gesture) override;
  void onGestureDispatch(kusanagi::bar::Gesture gesture, const kusanagi::bar::WidgetAction& action) override;

  // The provider's data for the options in `o`: the spec, then the effective spec when a state changes them.
  [[nodiscard]] Data collect(const nlohmann::json& o) const;

  // Custom modules run `exec` with sh -c every `interval` seconds, or read its lines as a `stream`. The child
  // dies with the module.
  struct CustomRun {
    std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
    std::atomic<int> pgid{0};       // the child leads its own process group (process.cpp)
    std::atomic<bool> exited{false};
  };
  void customSync(const nlohmann::json& eff);
  void customKick();
  void customStop();
  void customTake(const std::string& line);
  [[nodiscard]] nlohmann::json effective(const Data& data, std::vector<std::string>& states) const;

  KusanagiModuleServices m_services;
  nlohmann::json m_spec;
  std::string m_type;
  bool m_retained = false;

  InputArea* m_area = nullptr;
  Label* m_label = nullptr;
  Image* m_logo = nullptr;     // the Kusanagi logo, for a launcher with "logo": true
  bool m_logoLoaded = false;
  float m_baseFont = 12.0F;    // the effective font size, before hoverGrow
  float m_textHeight = 0.0F;   // lines * (round(ascent) + round(descent)), the classic line pitch
  float m_textAscent = 0.0F;   // the first baseline below the top: round(ascent)
  bool m_hovered = false;
  bool m_altOn = false;        // toggled by the "alt" action to show the alt format
  std::string m_lastTip;
  bool m_vertical = false;
  std::string m_lastMarkup;
  float m_padStart = 10.0F;
  float m_padEnd = 10.0F;
  float m_gapStart = 2.0F;   // outside the box, [2, 2] unless set
  float m_gapEnd = 2.0F;
  float m_insetEdge = 0.0F;  // the box's inset across the bar: [edge, inner]
  float m_insetInner = 0.0F;
  float m_groupEdge = 0.0F;  // the group's box inset; the module spans the group's inner thickness
  float m_groupInner = 0.0F;
  bool m_farEdge = false;    // bottom or right bar: insets count from the other side
  KusanagiBox m_box;
  nlohmann::json m_eff = nlohmann::json::object();
  // Media marquee: a long "title - artist" scrolls one character at a time while playing.
  Timer m_marqueeTimer;
  std::size_t m_marquee = 0;
  std::string m_marqueeTrack;
  // Taskbar modules: pinned apps and windows, drawn after any text.
  std::unique_ptr<KusanagiTaskbar> m_taskbar;
  Node* m_taskbarNode = nullptr;
  float m_taskbarCross = -1.0F;
  void syncTaskbar(Renderer& renderer, const nlohmann::json& eff);
  // Title modules keep the last real window while an overlay has the focus.
  mutable std::optional<ActiveToplevel> m_lastWindow;
  // The module's states as of the last update, for when-state actions.
  std::vector<std::string> m_states;
  float m_opacity = 1.0F;
  float m_grow = 0.0F;        // hoverGrow, easing in / out
  float m_growTarget = 0.0F;
  // Custom modules
  std::shared_ptr<int> m_alive = std::make_shared<int>(0); // deferred callbacks check it before touching the widget
  nlohmann::json m_customData = nlohmann::json{{"text", ""}};
  std::string m_customCmd;
  bool m_customStream = false;
  bool m_customPaused = false;
  bool m_customRunning = false;
  double m_customEvery = -1.0;
  std::uint64_t m_customGen = 0;
  std::shared_ptr<CustomRun> m_customRun;
  Timer m_customTimer;
  Timer m_customRestart;
};
