#pragma once

// Kusanagi's workspace indicator: pills, dots, numbers, roman, kanji, custom glyphs or dwl-style tags.
// workspaces.shown slots are always visible and more appear while occupied or active. Click switches;
// scrolling is the bar's (workspace:prev / workspace:next actions).

#include "shell/bar/widget.h"
#include "shell/bar/widgets/kusanagi_box.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

class Box;
class Label;
class InputArea;
class CompositorPlatform;
struct wl_output;

class KusanagiWorkspacesWidget : public Widget {
public:
  KusanagiWorkspacesWidget(CompositorPlatform& platform, wl_output* output, std::string specJson);

  void create() override;
  [[nodiscard]] bool wantsSecondTick() const override { return false; } // workspace events drive it

private:
  struct Slot {
    int n = 0;
    std::string id;
    bool active = false;
    bool occupied = false;
    bool urgent = false;
  };
  struct SlotNodes {
    InputArea* area = nullptr;
    Box* halo = nullptr;      // glow under the active or urgent dot
    Box* dot = nullptr;
    Box* block = nullptr;     // dwl
    Box* mark = nullptr;      // dwl occupied square
    Box* underline = nullptr; // text styles
    Label* label = nullptr;
    bool hovered = false;
  };

  void doLayout(Renderer& renderer, float containerWidth, float containerHeight) override;
  void doUpdate(Renderer& renderer) override;
  [[nodiscard]] std::optional<kusanagi::bar::WidgetAction> gestureOverride(kusanagi::bar::Gesture gesture) override;
  [[nodiscard]] nlohmann::json effectiveSpec() const;
  void applyOptions(const nlohmann::json& eff);
  [[nodiscard]] std::vector<Slot> collect() const;
  [[nodiscard]] std::string glyph(int n) const;
  void rebuild(std::size_t count);

  CompositorPlatform& m_platform;
  wl_output* m_output = nullptr;
  std::string m_style = "pills";
  int m_shown = 5;
  std::string m_activeColor = "accent";
  bool m_glow = true;
  std::vector<std::string> m_icons;
  float m_fontSize = 12.0F;
  float m_padStart = 10.0F; // the module's padding, [10, 10] unless set
  float m_padEnd = 10.0F;
  float m_gapStart = 2.0F;
  float m_gapEnd = 2.0F;
  float m_insetEdge = 0.0F;
  float m_insetInner = 0.0F;
  float m_groupEdge = 0.0F;  // the group's box inset; the module spans the group's inner thickness
  float m_groupInner = 0.0F;
  bool m_farEdge = false;    // bottom or right bar: insets count from the other side
  nlohmann::json m_spec = nlohmann::json::object();
  nlohmann::json m_eff = nlohmann::json::object(); // m_spec with the active `when` states applied
  std::vector<std::string> m_states;                // hover, alt
  bool m_altOn = false;
  KusanagiBox m_box;
  Node* m_slotsNode = nullptr;
  InputArea* m_hoverArea = nullptr; // the module's hover area, over its box
  bool m_hovered = false;

  std::vector<Slot> m_slots;
  std::vector<SlotNodes> m_nodes;
  bool m_vertical = false;
};
