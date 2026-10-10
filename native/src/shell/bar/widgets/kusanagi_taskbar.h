#pragma once

// The "taskbar" bar module's content: pinned apps (dock.pinned, or the module's own "pinned") and whatever
// else is open, either grouped (one icon per app, with dots or a line for its windows) or one per window,
// optionally with titles. KusanagiModuleWidget owns one and forwards the module's clicks to it. The items
// have no hover state or tooltip of their own; the module takes the hover.

#include "system/icon_resolver.h"
#include "ui/controls/label.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class AnimationManager;
class Box;
class CompositorPlatform;
class Image;
class Node;
class Renderer;
struct zwlr_foreign_toplevel_handle_v1;

class KusanagiTaskbar {
public:
  explicit KusanagiTaskbar(CompositorPlatform* platform);
  ~KusanagiTaskbar();

  // The items' parent; the module positions it.
  [[nodiscard]] std::unique_ptr<Node> create();

  struct Look {
    nlohmann::json eff;          // the module's effective spec
    bool vertical = false;
    std::string edge = "top";    // the bar's edge (indicators sit on a side bar's outer side)
    float cross = 22.0F;         // the module's thickness inside its inset
    float baseFont = 12.0F;      // the titles' font size
    std::string font;            // eff.font, else the bar's font
    float scale = 1.0F;
    float fontScale = 1.0F;      // the bar's font scale (Widget::fontScale)
    int fontWeight = 400;        // the bar's label weight (what the module labels use)
  };
  // Syncs the model (pins and toplevels) and every item's look. Returns true when the size changed.
  bool sync(Renderer& renderer, const Look& look, AnimationManager* animations);

  [[nodiscard]] float width() const noexcept { return m_width; }
  [[nodiscard]] float height() const noexcept { return m_height; }
  [[nodiscard]] bool empty() const noexcept { return m_items.empty(); }

  // A click at (x, y) in the container's coordinates. Returns false when no item is there.
  bool click(float x, float y, std::uint32_t button);

private:
  struct Window {
    zwlr_foreign_toplevel_handle_v1* handle = nullptr;
    std::string title;
    std::string appId;
    bool activated = false;
    std::uint64_t order = 0;
  };
  struct Item {
    std::string key;             // "a:<app>" for an app, "w:<n>" for the n-th window
    bool isApp = false;
    std::string app;             // its app (desktop entry id, else the raw app id)
    zwlr_foreign_toplevel_handle_v1* window = nullptr;   // ungrouped: its window
    float x = 0.0F, y = 0.0F, w = 0.0F, h = 0.0F;
    Node* node = nullptr;
    Box* bg = nullptr;
    Box* tile = nullptr;         // no icon anywhere: the app's first letter on a tile
    Label* letter = nullptr;
    Image* icon = nullptr;
    std::string iconPath;
    Label* title = nullptr;
    std::vector<Box*> dots;
    Box* line = nullptr;
    float lineLen = -1.0F;       // the line indicator's target length (it eases there)
  };

  void rebuild(const std::vector<std::string>& keys);
  [[nodiscard]] std::string keyOf(const std::string& appId);
  [[nodiscard]] std::string nameOf(const std::string& app) const;
  [[nodiscard]] std::string iconOf(const std::string& app, int size); // "" when there is none
  [[nodiscard]] std::vector<const Window*> windowsOf(const std::string& app) const;
  void launch(const std::string& app);
  void activateApp(const std::string& app);
  void togglePin(const std::string& app);

  CompositorPlatform* m_platform = nullptr;
  Node* m_root = nullptr;
  std::vector<Window> m_windows;
  std::vector<Item> m_items;
  std::vector<std::string> m_pins;
  std::unordered_map<std::string, std::string> m_keyCache;  // app id to app
  std::unordered_map<std::string, std::string> m_iconCache; // app to icon path
  std::uint64_t m_entriesVersion = 0;
  IconResolver m_icons;
  // Matches the classic look: titles use unhinted, unrounded advances. A probe at 10x the size measures them
  // and the difference goes in as letter spacing, cached per title.
  std::unique_ptr<Label> m_probe;
  struct Title {
    std::string markup;
    float width = 0.0F; // its advance
  };
  std::unordered_map<std::string, Title> m_titleMarkup;
  float m_width = 0.0F;
  float m_height = 0.0F;
};
