#pragma once

// Wallpaper picker, panel id "wallpaper": a thumbnail grid of wallpaper.folder in a centred card. Type to
// filter, Enter or click applies, Ctrl+R picks a random one. Thumbnails come from the shared ThumbnailService
// and are released when the picker closes.

#include "render/animation/animation_manager.h"
#include "render/core/thumbnail_service.h"
#include "shell/panel/panel.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Box;
class ConfigService;
class Input;
class InputArea;
class Label;
class VirtualGridView;
class WallpaperScanner;
class WaylandConnection;

class KusanagiWallpaperPanel : public Panel {
public:
  struct Item {
    std::string name;    // file name
    std::string display; // without the extension
    std::string path;    // canonical path
    std::string lower;   // lower-case name, for the filter
  };

  KusanagiWallpaperPanel(
      WaylandConnection* wayland, ConfigService* config, ThumbnailService* thumbnails, WallpaperScanner* scanner
  );
  ~KusanagiWallpaperPanel() override;

  void create() override;
  void onOpen(std::string_view context) override;
  void onClose() override;
  [[nodiscard]] bool handleGlobalKey(std::uint32_t sym, std::uint32_t modifiers, bool pressed, bool preedit) override;

  [[nodiscard]] float preferredWidth() const override { return 0.0F; }
  [[nodiscard]] float preferredHeight() const override { return 0.0F; }
  [[nodiscard]] bool coversOutput() const noexcept override { return true; }
  [[nodiscard]] bool dismissOnOutsideClick() const override { return false; }
  [[nodiscard]] bool hasDecoration() const override { return false; }
  [[nodiscard]] LayerShellKeyboard keyboardMode() const override { return LayerShellKeyboard::Exclusive; }
  [[nodiscard]] LayerShellLayer layer() const override { return LayerShellLayer::Overlay; }
  [[nodiscard]] InputArea* initialFocusArea() const override;
  bool beginCloseAnimation(std::function<void()> done) override;

private:
  class Adapter;

  void doLayout(Renderer& renderer, float width, float height) override;
  void doUpdate(Renderer& renderer) override;
  void placeCard();
  void startOpenAnimation();
  void scan();
  void loadScan();
  void applyFilter();
  void select(std::size_t index);
  void move(int delta);
  void apply(std::size_t index);
  void applyRandom();
  void setWallpaper(const std::string& path);

  WaylandConnection* m_wayland = nullptr;
  ConfigService* m_config = nullptr;
  ThumbnailService* m_thumbnails = nullptr;
  WallpaperScanner* m_scanner = nullptr;
  std::unique_ptr<Adapter> m_adapter;
  ThumbnailService::Subscription m_thumbSub;
  bool m_thumbRefresh = false;

  std::string m_folder; // canonical
  std::string m_current;
  std::vector<Item> m_all;
  std::vector<Item> m_items; // filtered
  std::string m_query;
  std::size_t m_selected = 0;
  bool m_scanPending = false;
  bool m_centerPending = false;
  std::size_t m_columns = 4;

  float m_progress = 0.0F;
  bool m_closing = false;
  float m_surfaceW = 0.0F;
  float m_surfaceH = 0.0F;
  float m_cardX = 0.0F;
  float m_cardY = 0.0F;
  float m_cardW = 0.0F;
  float m_cardH = 0.0F;
  bool m_randomHover = false;
  bool m_inputFocused = false;

  Node* m_rootNode = nullptr;
  Box* m_backdrop = nullptr;
  InputArea* m_backdropArea = nullptr;
  Node* m_cardGroup = nullptr;
  Box* m_shadow = nullptr;
  Box* m_card = nullptr;
  InputArea* m_cardArea = nullptr;
  Label* m_titleIcon = nullptr;
  Label* m_title = nullptr;
  Box* m_fieldPill = nullptr;
  Label* m_fieldIcon = nullptr;
  Label* m_placeholder = nullptr;
  Input* m_input = nullptr;
  InputArea* m_randomArea = nullptr;
  Box* m_randomFace = nullptr;
  Label* m_randomIcon = nullptr;
  VirtualGridView* m_grid = nullptr;
  Label* m_empty = nullptr;
};
