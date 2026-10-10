#pragma once

// Single-line text input pill with an optional icon and a placeholder while empty. The border turns to the
// accent while focused. `accepted` fires on Enter. The Look also covers the polkit dialog's password box.

#include "render/scene/input_area.h"
#include "ui/palette.h"

#include <functional>
#include <string>

class Box;
class Input;
class Label;
class Renderer;

namespace kusanagi::cp {

  class Field : public InputArea {
  public:
    struct Look {
      float height = 34.0F;
      float radius = -1.0F;            // < 0: a pill
      float fontPx = 12.0F;
      float placeholderPx = 12.0F;
      float iconPx = 14.0F;
      float idleBorderAlpha = 0.08F;   // text colour alpha of the border while not focused
      bool placeholderWhileFocused = false;
      bool password = false;
      bool animateBorder = true;       // fade the border colour on focus changes
    };

    explicit Field(Look look, char32_t icon = 0);

    void setPlaceholder(const std::string& text);
    void setText(const std::string& text);
    [[nodiscard]] const std::string& text() const;
    void setPasswordRevealed(bool revealed);
    void setEnabled(bool enabled);
    [[nodiscard]] bool focused() const noexcept { return m_focused; }
    // The focusable text area, for PanelManager::focusArea and Panel::initialFocusArea.
    [[nodiscard]] InputArea* focusArea() const;

    void setOnAccepted(std::function<void(const std::string&)> fn) { m_onAccepted = std::move(fn); }
    void setOnEdited(std::function<void(const std::string&)> fn) { m_onEdited = std::move(fn); }
    // Keys the text input didn't use, like Escape. Return true when handled.
    void setOnKey(std::function<bool(std::uint32_t sym, std::uint32_t mods)> fn) { m_onKey = std::move(fn); }

    void layout(Renderer& renderer);

  private:
    void restyle(bool animate);
    void syncPlaceholder();

    Look m_look;
    Box* m_bg = nullptr;
    Label* m_icon = nullptr;
    Input* m_input = nullptr;
    Label* m_placeholder = nullptr;
    bool m_focused = false;
    ColorSpec m_border;
    std::function<void(const std::string&)> m_onAccepted;
    std::function<void(const std::string&)> m_onEdited;
    std::function<bool(std::uint32_t, std::uint32_t)> m_onKey;
  };

} // namespace kusanagi::cp
