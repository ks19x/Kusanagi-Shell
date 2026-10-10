#pragma once

// Where the bar's clock island is on each output, so the control panel can grow out of it. The bar publishes
// it whenever its geometry changes and the panel reads it when it opens. Output-local logical pixels; `edge`
// is the bar's edge (top, bottom, left or right).

#include <functional>
#include <optional>
#include <string>

struct wl_output;

namespace kusanagi {

  struct IslandRect {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
    std::string edge = "top";
  };

  void publishClockIsland(wl_output* output, std::optional<IslandRect> rect);
  [[nodiscard]] std::optional<IslandRect> clockIsland(wl_output* output);

  // While the control panel is showing, the bar hides the clock island it grew out of.
  void setPanelShowing(bool showing);
  [[nodiscard]] bool panelShowing();
  void setPanelShowingCallback(std::function<void(bool)> callback); // set by the bar

} // namespace kusanagi
