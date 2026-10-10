#pragma once

// The control panel's building blocks: text and icons, cards, icon buttons, tiles, sliders, the segmented
// picker, switch and chip. Each block is an InputArea with a fixed layout: set its size, then call
// layout(renderer). Hover, press and state changes tween through the panel's AnimationManager, with base
// durations that kusanagi::ms() scales by look.animSpeed.

#include "render/scene/input_area.h"
#include "ui/palette.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Box;
class Label;
class Renderer;

namespace kusanagi::cp {

  // Icons are Material Design glyphs from this Nerd Font, by codepoint.
  inline constexpr const char* kIconFont = "JetBrainsMono Nerd Font";

  [[nodiscard]] std::string utf8(char32_t codepoint);

  // A label in the look font.
  [[nodiscard]] std::unique_ptr<Label> text(const std::string& s, float px, bool bold = false,
                                            const ColorSpec& color = colorSpecFromRole(ColorRole::OnSurface));
  [[nodiscard]] std::unique_ptr<Label> icon(char32_t codepoint, float px,
                                            const ColorSpec& color = colorSpecFromRole(ColorRole::OnSurface));
  // Letter-spaced text through Pango markup, so `s` must be plain text.
  void setSpacedText(Label& label, const std::string& s, float letterSpacingPx);

  [[nodiscard]] inline ColorSpec textA(float a) { return colorSpecFromRole(ColorRole::OnSurface, a); }
  [[nodiscard]] inline ColorSpec accent(float a = 1.0F) { return colorSpecFromRole(ColorRole::Primary, a); }
  [[nodiscard]] inline ColorSpec bgPanel(float a = 1.0F) { return colorSpecFromRole(ColorRole::Surface, a); }
  [[nodiscard]] inline ColorSpec dim() { return colorSpecFromRole(ColorRole::OnSurfaceVariant); }
  [[nodiscard]] inline ColorSpec danger() { return colorSpecFromRole(ColorRole::Error); }
  // A card corner, a bit tighter than look.radius.
  [[nodiscard]] float cardRadius();

  // Easing curves on a 0..1 progress.
  [[nodiscard]] float bezier(float x1, float y1, float x2, float y2, float t);
  [[nodiscard]] float outQuint(float t);
  [[nodiscard]] float outCubic(float t);
  [[nodiscard]] float outBack(float t, float overshoot);

  // Tweens a colour on `owner`, cancelling its previous tween. The in-between steps are fixed colours and
  // the last one is `to` itself, so it keeps following the palette.
  void tweenColor(Node& owner, const ColorSpec& from, const ColorSpec& to, int baseMs,
                  std::function<void(const ColorSpec&)> apply);
  // Tweens Node::scale for press feedback, with an OutBack overshoot.
  void tweenScale(Node& node, float to, int baseMs);

  // Centres a measured label in a box.
  void centerIn(Label& label, float x, float y, float w, float h);

  // Soft inset surface. `hoverable` brightens it to `hoverAlpha` under the pointer, like the stat and
  // weather cards.
  class Card : public InputArea {
  public:
    explicit Card(bool hoverable = false, float hoverAlpha = 0.08F);
    void setSize(float width, float height) override;
    [[nodiscard]] Box* surface() const noexcept { return m_bg; }

  private:
    Box* m_bg = nullptr;
    ColorSpec m_fill;
    float m_hoverAlpha;
  };

  // Round icon button (header actions, media controls).
  class IconButton : public InputArea {
  public:
    IconButton(char32_t codepoint, float iconPx = 16.0F, bool filled = false);
    void setIcon(char32_t codepoint);
    void setFilled(bool filled);
    void layout(Renderer& renderer);
    void setOnActivate(std::function<void()> fn) { m_onActivate = std::move(fn); }

  private:
    void restyle(bool animate);
    Box* m_bg = nullptr;
    Label* m_icon = nullptr;
    bool m_filled = false;
    ColorSpec m_bgColor;
    ColorSpec m_iconColor;
    std::function<void()> m_onActivate;
  };

  // Quick toggle or action. The look is "cards", "pills" or "icons".
  class Tile : public InputArea {
  public:
    explicit Tile(std::string look);
    [[nodiscard]] static float heightFor(const std::string& look);
    // True when a text changed and the tile needs a layout.
    bool setContent(char32_t codepoint, const std::string& label, const std::string& sub, bool on);
    void layout(Renderer& renderer);
    void setOnActivate(std::function<void()> fn) { m_onActivate = std::move(fn); }

  private:
    void restyle(bool animate);
    std::string m_look;
    Box* m_bg = nullptr;
    Label* m_icon = nullptr;
    Label* m_label = nullptr;
    Label* m_sub = nullptr;
    char32_t m_codepoint = 0;
    bool m_on = false;
    ColorSpec m_bgColor;
    ColorSpec m_fgColor;
    std::function<void()> m_onActivate;
  };

  // Chunky pill slider, or a slim one with an icon, a thin track with a knob and the value.
  class Slider : public InputArea {
  public:
    explicit Slider(bool slim);
    void setIcon(char32_t codepoint);
    bool setLabel(const std::string& label);  // true when it changed
    void setValue(float value);  // 0..1, ignored while dragging
    void setMuted(bool muted);
    void layout(Renderer& renderer);
    void setOnMoved(std::function<void(float)> fn) { m_onMoved = std::move(fn); }
    void setOnIconClicked(std::function<void()> fn) { m_onIconClicked = std::move(fn); }
    [[nodiscard]] bool dragging() const noexcept { return m_dragging; }

  private:
    [[nodiscard]] float trackX() const;
    [[nodiscard]] float trackW() const;
    [[nodiscard]] float at(float localX) const;
    void moveTo(float v);
    void applyFill(float shown);
    void restyleTexts();

    bool m_slim;
    float m_value = 0.0F;
    float m_shown = 0.0F;  // the fill as drawn, animated toward m_value
    bool m_muted = false;
    bool m_dragging = false;
    Box* m_track = nullptr;
    Box* m_fill = nullptr;
    Box* m_knob = nullptr;
    InputArea* m_iconArea = nullptr;
    Label* m_icon = nullptr;
    Label* m_label = nullptr;
    Label* m_valueText = nullptr;
    std::function<void(float)> m_onMoved;
    std::function<void()> m_onIconClicked;
  };

  // One-of-N picker with a sliding highlight.
  struct Segment {
    std::string label;
    int value = 0;
    char32_t icon = 0;
  };

  class Segmented : public InputArea {
  public:
    explicit Segmented(float fontPx = 11.0F);
    void setOptions(std::vector<Segment> options, int current);
    void setCurrent(int value, bool animate);
    void layout(Renderer& renderer);
    void setOnPicked(std::function<void(int)> fn) { m_onPicked = std::move(fn); }

  private:
    struct Cell {
      InputArea* area = nullptr;
      Label* icon = nullptr;
      Label* label = nullptr;
      ColorSpec color;
    };
    void restyle(bool animate);
    [[nodiscard]] int indexOf(int value) const;

    float m_fontPx;
    std::vector<Segment> m_options;
    int m_current = 0;
    Box* m_bg = nullptr;
    Box* m_highlight = nullptr;
    std::vector<Cell> m_cells;
    float m_highlightX = 3.0F;
    bool m_sliding = false;
    std::function<void(int)> m_onPicked;
  };

  // On/off switch with a springy knob.
  class Switch : public InputArea {
  public:
    Switch();
    void setOn(bool on, bool animate);
    void setOnToggled(std::function<void(bool)> fn) { m_onToggled = std::move(fn); }

  private:
    bool m_on = false;
    Box* m_bg = nullptr;
    Box* m_knob = nullptr;
    ColorSpec m_bgColor;
    ColorSpec m_knobColor;
    std::function<void(bool)> m_onToggled;
  };

  // Small pill with an optional icon, as wide as its content.
  class Chip : public InputArea {
  public:
    Chip(const std::string& label, char32_t codepoint = 0, bool on = false);
    void setOn(bool on);
    void layout(Renderer& renderer);
    void setOnActivate(std::function<void()> fn) { m_onActivate = std::move(fn); }

  private:
    void restyle(bool animate);
    Box* m_bg = nullptr;
    Label* m_icon = nullptr;
    Label* m_label = nullptr;
    bool m_on = false;
    ColorSpec m_bgColor;
    std::function<void()> m_onActivate;
  };

  // Pick-list row (sound devices, Wi-Fi networks): icon, text and a trailing icon. The selected row is
  // tinted with the accent and its text is bold.
  class ListRow : public InputArea {
  public:
    struct Look {
      float height = 34.0F;
      float iconPx = 16.0F;
      float trailingPx = 14.0F;
      ColorSpec iconColor = colorSpecFromRole(ColorRole::OnSurfaceVariant);  // when not selected
      ColorSpec trailingColor = colorSpecFromRole(ColorRole::Primary);
      bool trailingOnlySelected = true;  // true for a check mark, false for an always-shown lock
    };
    explicit ListRow(Look look);
    // True when the text changed.
    bool set(char32_t icon, const std::string& text, char32_t trailing, bool selected);
    void layout(Renderer& renderer);
    void setOnActivate(std::function<void()> fn) { m_onActivate = std::move(fn); }

  private:
    void restyle(bool animate);
    Look m_look;
    Box* m_bg = nullptr;
    Label* m_icon = nullptr;
    Label* m_text = nullptr;
    Label* m_trailing = nullptr;
    bool m_selected = false;
    bool m_hasTrailing = false;
    ColorSpec m_bgColor;
    std::function<void()> m_onActivate;
  };

  // A section heading inside a card, like "OUTPUT" or "CPU LOAD".
  [[nodiscard]] std::unique_ptr<Label> caption(const std::string& s);

  // Runs `command` on a worker thread and hands its stdout to `done` on the main loop, unless
  // `alive` has expired by then (the page went away).
  void capture(std::vector<std::string> command, std::weak_ptr<void> alive, std::function<void(std::string)> done);

} // namespace kusanagi::cp
