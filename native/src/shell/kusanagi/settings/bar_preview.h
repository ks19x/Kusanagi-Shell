#pragma once

// Scaled preview of one bar spec (raw, as in settings.json "bars") drawn with the bar's real module widgets.
// The bar editor uses it pickable, so modules can be selected and dragged in and out of islands; the
// template cards use it as a snapshot that is drawn once.

#include "shell/kusanagi/settings/sp_kit.h"

#include <functional>
#include <memory>
#include <string>

namespace kusanagi::sp {

  struct BarPreviewOpts {
    bool snapshot = false; // Drawn once (gallery cards); otherwise live data refreshed every second.
    bool pickable = false; // Clicks select modules or islands and drags move them (the editor).
    // Width of the virtual screen for a given item width. Defaults to max(800, round(width / 0.62)).
    std::function<float(float width)> screenW;
    float screenH = 720.0F;
    bool desktop = true; // Draw a backdrop behind the bar.
    float fixedScale = 0.0F; // If > 0, draw at this scale showing the screen's top-left (side bars on preset cards).
  };

  class BarPreview : public Item {
  public:
    // `bar` is re-read on every sync and a changed spec rebuilds the preview. `height` maps width to height.
    BarPreview(std::function<json()> bar, std::function<float(float width)> height, BarPreviewOpts opts = {});
    ~BarPreview() override;
    // Click on a module: section, entry index, module index in the group (-1 for the entry itself).
    BarPreview* onPicked(std::function<void(const std::string& section, int gi, int mi)> fn);
    // Drop: `from` is "sec:gi:mi", `to` is a module or island key or "section:<name>" to append there.
    BarPreview* onMoved(std::function<void(const std::string& from, const std::string& to, bool after)> fn);
    // Key of the selection to outline ("sec:gi:mi").
    BarPreview* selKey(std::function<std::string()> fn);
    float place(Renderer& renderer, float width) override;
    void sync() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> m;
  };

} // namespace kusanagi::sp
