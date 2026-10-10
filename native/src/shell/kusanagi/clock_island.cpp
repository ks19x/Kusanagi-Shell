#include "shell/kusanagi/clock_island.h"

#include <unordered_map>
#include <utility>

namespace kusanagi {

  namespace {
    std::unordered_map<wl_output*, IslandRect>& islands() {
      static std::unordered_map<wl_output*, IslandRect> map;
      return map;
    }
    bool g_panelShowing = false;
    std::function<void(bool)>& panelCallback() {
      static std::function<void(bool)> cb;
      return cb;
    }
  } // namespace

  void publishClockIsland(wl_output* output, std::optional<IslandRect> rect) {
    if (rect) {
      islands()[output] = std::move(*rect);
    } else {
      islands().erase(output);
    }
  }

  void setPanelShowing(bool showing) {
    if (g_panelShowing == showing) return;
    g_panelShowing = showing;
    if (panelCallback()) panelCallback()(showing);
  }

  bool panelShowing() { return g_panelShowing; }

  void setPanelShowingCallback(std::function<void(bool)> callback) { panelCallback() = std::move(callback); }

  std::optional<IslandRect> clockIsland(wl_output* output) {
    const auto it = islands().find(output);
    if (it == islands().end()) return std::nullopt;
    return it->second;
  }

} // namespace kusanagi
