#pragma once

// The look of a Kusanagi bar module, group or bar: fill (a colour or a two-colour gradient along the bar),
// border, radius, powerline caps and an indicator line. Cap triangles are a large rotated box inside an
// axis-aligned clip (a half-plane), so no polygon shader is needed.

#include "render/core/color.h"
#include "render/core/render_styles.h"
#include "ui/palette.h"

#include <nlohmann/json.hpp>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>

class AnimationManager;
class Box;
class Node;

// The Kusanagi look of each bar (key "k0") and of each group drawn as a capsule group ("k0_start_1"; an
// island bar's section capsule "k0_center" carries the bar's look). config/kusanagi_import.cpp normalises
// them on every settings load and shell/bar/bar.cpp draws them with a KusanagiBox.
namespace kusanagi_bar {
  void setLooks(std::map<std::string, nlohmann::json> looks);
  [[nodiscard]] const nlohmann::json* look(const std::string& key);
  // kusanagi::color, plus "warn": the danger colour tinted towards amber.
  [[nodiscard]] ColorSpec color(const std::string& token, const ColorSpec& fallback = colorSpecFromRole(ColorRole::OnSurface));
} // namespace kusanagi_bar

class KusanagiBox {
public:
  ~KusanagiBox();

  // The node to add under the widget's root, behind the label. It owns its children.
  [[nodiscard]] std::unique_ptr<Node> create();
  // A solid fill fades to a new colour (e.g. on hover); `redraw` repaints the bar while it runs.
  void setAnimator(AnimationManager* animations, std::function<void()> redraw) {
    m_animations = animations;
    m_redraw = std::move(redraw);
  }

  // Whether this spec draws anything at all.
  [[nodiscard]] static bool draws(const nlohmann::json& eff);
  // Cap length along the bar for a cap kind at this thickness.
  [[nodiscard]] static float capSize(const std::string& kind, float cross);

  // Lays the box out at (x, y, w, h), the module's box area with caps included.
  void apply(const nlohmann::json& eff, bool hovered, bool vertical, float x, float y, float w, float h, float scale);
  // Runs the last apply() again so colours pick up the current palette.
  void reapply();

private:
  struct HalfPlane {
    Node* clip = nullptr;
    Box* fill = nullptr;
  };
  struct Cap {
    Node* node = nullptr;
    Box* back = nullptr;
    std::array<HalfPlane, 2> parts{};
  };

  void layoutCap(Cap& cap, const std::string& kind, bool atEnd, bool vertical, float c, float h, const nlohmann::json& paint,
                 const nlohmann::json& back);

  struct Last {
    nlohmann::json eff;
    bool hovered = false;
    bool vertical = false;
    float x = 0, y = 0, w = 0, h = 0, scale = 1;
  };
  Last m_last;
  AnimationManager* m_animations = nullptr;
  std::function<void()> m_redraw;
  RoundedRectStyle m_style;
  Color m_shownFill{};
  Color m_targetFill{};
  bool m_hasFill = false;
  Node* m_root = nullptr;
  Box* m_body = nullptr;
  Box* m_line = nullptr;
  Cap m_start;
  Cap m_end;
};
