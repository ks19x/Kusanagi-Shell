#pragma once

// Settings > Appearance > Terminal colours (terminal.colors). While it is on, every palette change writes a
// matching theme for foot, kitty, Alacritty, Ghostty and WezTerm into ~/.config/kusanagi/terminal/, hooks it
// into the config of each of those that is installed (one marked block, the original kept as .bak-kusanagi),
// and recolours the terminal windows that are already open. Switching it off takes the hooks out again and gives
// open windows their own colours back.

#include "core/timer_manager.h"
#include "ui/signal.h"

#include <string>

namespace kusanagi {

  // The terminals Kusanagi can recolour that are installed, by display name ("foot, kitty").
  [[nodiscard]] std::string terminalsFound();

  class TerminalColors {
  public:
    static TerminalColors& instance();

    // Follows the palette from now on and applies once the theme has loaded.
    void start();
    // Called on a config reload: the option may have been switched.
    void onConfigReload();

  private:
    TerminalColors() = default;
    void apply();

    Signal<>::ScopedConnection m_paletteConn;
    Timer m_later;
    bool m_started = false;
    bool m_on = false;
  };

} // namespace kusanagi
