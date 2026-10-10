#pragma once

// Compositor details for the display settings: which compositor is running, its monitor list, and the
// windows.override gaps and borders. While the override is on they are set live (mmsg, hyprctl or swaymsg). On Mango
// they are also kept across reloads in ~/.config/kusanagi/mango.conf, which the user's mango config sources
// and which is emptied again when the override goes off. Nothing runs while the override was never on.

#include <string>
#include <vector>

namespace kusanagi::wm {

  enum class Kind { Mango, Hyprland, Niri, Sway, Labwc, Kde, Dwl, Other };

  [[nodiscard]] Kind kind();
  // "MangoWM", "Hyprland", "niri", "sway", "labwc", "KDE Plasma", "dwl" or "Wayland".
  [[nodiscard]] std::string name();
  // The compositor's config file, or "" when there's none to open.
  [[nodiscard]] std::string configFile();

  struct Monitor {
    std::string name;
    int w = 0;
    int h = 0;
    double hz = 0.0;
    double scale = 1.0;
    int x = 0;
    int y = 0;
  };
  // The command that lists monitors, empty when this compositor has none (the Wayland outputs are used then).
  [[nodiscard]] std::vector<std::string> monitorsCommand();
  [[nodiscard]] std::vector<Monitor> parseMonitors(const std::string& text);

  // Gaps and borders
  struct Layout {
    int gapsIn = 8; // px between two windows; Hyprland's gaps_in is per side, so half of this
    int gapsOut = 8;
    int border = 2;
  };
  // Mango, Hyprland and sway. labwc, KWin and dwl keep gaps and borders in their own config only, so the
  // Settings group is hidden there.
  [[nodiscard]] bool canSetLayout();
  // The compositor config's own values: the sliders' starting point and what turning the override off restores.
  [[nodiscard]] Layout layout();
  // Once at startup.
  void start();
  // After settings.json changed. Coalesced over 30 ms so slider drags don't flood the compositor.
  void settingsChanged();

} // namespace kusanagi::wm
