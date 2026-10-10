#pragma once

// Clipboard history panel ("clipboard"): the cliphist history on the left (text, colour codes with a swatch,
// links, image thumbnails) and a preview of the selection on the right, in the launcher's card style.
// Image entries are decoded once into ~/.cache/kusanagi/clip.

#include "shell/kusanagi/slide_highlight.h"
#include "shell/panel/panel.h"

#include "render/core/color.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class AsyncTextureCache;
class Box;
class Image;
class Input;
class InputArea;
class Label;
class ScrollView;
class VirtualListView;

class KusanagiClipboardPanel : public Panel {
public:
  struct Entry {
    std::string id;
    std::uint64_t key = 0;
    bool image = false;
    std::string value;   // text preview, or the decoded image's path
    std::string oneLine; // whitespace collapsed (the list row)
    std::string lower;
    bool colour = false;
    Color swatch{};
    bool url = false;
  };

  explicit KusanagiClipboardPanel(AsyncTextureCache* asyncTextures);
  ~KusanagiClipboardPanel() override;

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
  void placeCard();
  void startOpenAnimation();
  void load();
  void setEntries(const std::string& listing);
  void applyFilter();
  void select(std::size_t index, bool scroll);
  void step(int delta);
  void copy(std::size_t index);
  void remove(std::size_t index);
  void wipe();
  void layoutPreview(Renderer& renderer, float x, float y, float w, float h);
  [[nodiscard]] float rowHeight(std::size_t index) const;
  // Moves the highlight onto the selected row. With `animate` it slides and scrolls the list along.
  void syncHighlight(bool animate);

  AsyncTextureCache* m_asyncTextures = nullptr;
  std::unique_ptr<Adapter> m_adapter;
  std::shared_ptr<std::uint64_t> m_generation = std::make_shared<std::uint64_t>(0);

  std::vector<Entry> m_entries;
  std::vector<std::size_t> m_filtered; // indices into m_entries
  std::string m_query;
  std::size_t m_selected = 0;
  bool m_loading = false;
  std::string m_previewId; // what the preview pane shows now
  bool m_wipeHover = false;
  float m_listH = 0.0F;
  float m_listW = 0.0F;
  kusanagi::SlideHighlight m_highlight;
  bool m_highlightReset = true; // a new model / geometry: the highlight jumps instead of sliding

  float m_progress = 0.0F;
  bool m_closing = false;
  float m_cardX = 0.0F;
  float m_cardY = 0.0F;
  float m_cardW = 0.0F;
  float m_cardH = 0.0F;

  Node* m_rootNode = nullptr;
  Box* m_backdrop = nullptr;
  InputArea* m_backdropArea = nullptr;
  Node* m_cardGroup = nullptr;
  Box* m_shadow = nullptr;
  Box* m_card = nullptr;
  InputArea* m_cardArea = nullptr;
  Label* m_fieldIcon = nullptr;
  Label* m_placeholder = nullptr;
  Input* m_input = nullptr;
  InputArea* m_wipeArea = nullptr;
  Box* m_wipeFace = nullptr;
  Label* m_wipeIcon = nullptr;
  Label* m_wipeLabel = nullptr;
  Box* m_separator = nullptr;
  VirtualListView* m_list = nullptr;
  Box* m_previewCard = nullptr;
  ScrollView* m_previewScroll = nullptr;
  Label* m_previewText = nullptr;
  Box* m_previewSwatch = nullptr;
  Image* m_previewImage = nullptr;
  Label* m_emptyIcon = nullptr;
  Label* m_emptyLabel = nullptr;
};
