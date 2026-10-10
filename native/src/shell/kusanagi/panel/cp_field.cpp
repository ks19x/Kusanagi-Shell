#include "shell/kusanagi/panel/cp_field.h"

#include "render/core/renderer.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/panel/cp_controls.h"
#include "shell/panel/panel_manager.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"

#include <cmath>

namespace kusanagi::cp {

  Field::Field(Look look, char32_t codepoint) : m_look(look) {
    m_bg = static_cast<Box*>(addChild(ui::box({})));
    if (codepoint != 0) m_icon = static_cast<Label*>(addChild(icon(codepoint, m_look.iconPx, dim())));
    m_placeholder = static_cast<Label*>(addChild(cp::text("", m_look.placeholderPx, false, dim())));
    m_placeholder->setMaxLines(1);
    m_input = static_cast<Input*>(addChild(ui::input({
        .fontSize = m_look.fontPx,
        .controlHeight = m_look.height,
        .horizontalPadding = 0.0F,
        .clearButtonEnabled = false,
        .passwordMode = m_look.password,
        .frameVisible = false,
        .onChange =
            [this](const std::string& value) {
              syncPlaceholder();
              if (m_onEdited) m_onEdited(value);
            },
        .onSubmit =
            [this](const std::string& value) {
              if (m_onAccepted) m_onAccepted(value);
            },
        .onKeyEvent =
            [this](std::uint32_t sym, std::uint32_t mods) { return m_onKey ? m_onKey(sym, mods) : false; },
    })));
    m_input->setOnFocusGain([this]() {
      m_focused = true;
      syncPlaceholder();
      restyle(true);
    });
    m_input->setOnFocusLoss([this]() {
      m_focused = false;
      syncPlaceholder();
      restyle(true);
    });
    // A click anywhere on the pill, including the icon and margins, focuses the input.
    setOnPress([this](const PointerData& d) {
      if (d.pressed && m_input->inputArea() != nullptr && m_input->inputArea()->enabled()) {
        PanelManager::instance().focusArea(m_input->inputArea());
      }
    });
    restyle(false);
  }

  void Field::setPlaceholder(const std::string& s) { m_placeholder->setText(s); }

  void Field::setText(const std::string& s) {
    m_input->setValue(s);
    syncPlaceholder();
  }

  const std::string& Field::text() const { return m_input->value(); }

  void Field::setPasswordRevealed(bool revealed) { m_input->setPasswordRevealed(revealed); }

  void Field::setEnabled(bool enabled) {
    if (enabled == m_input->enabled()) return;
    // Disabling the field drops the focus, and with it the accent border and the caret.
    if (!enabled && m_focused) PanelManager::instance().focusArea(nullptr);
    m_input->setEnabled(enabled);
    m_input->setOpacity(1.0F);  // the text doesn't fade when disabled
  }

  InputArea* Field::focusArea() const { return m_input->inputArea(); }

  void Field::syncPlaceholder() {
    m_placeholder->setVisible(m_input->value().empty() && (m_look.placeholderWhileFocused || !m_focused));
  }

  void Field::restyle(bool animate) {
    const ColorSpec border = m_focused ? accent(0.7F) : textA(m_look.idleBorderAlpha);
    m_bg->setFill(textA(0.06F));
    if (animate && m_look.animateBorder) {
      tweenColor(*m_bg, m_border, border, 160, [this](const ColorSpec& c) { m_bg->setBorder(c, 1.0F); });
    } else {
      m_bg->setBorder(border, 1.0F);
    }
    m_border = border;
  }

  void Field::layout(Renderer& renderer) {
    const float w = width();
    const float h = m_look.height;
    InputArea::setSize(w, h);
    m_bg->setSize(w, h);
    m_bg->setRadius(m_look.radius < 0.0F ? h / 2.0F : m_look.radius);
    const float left = m_icon != nullptr ? 34.0F : 14.0F;
    if (m_icon != nullptr) {
      m_icon->measure(renderer);
      m_icon->setPosition(12.0F, std::round((h - m_icon->height()) / 2.0F));
    }
    const float iw = std::max(1.0F, w - left - 14.0F);
    if (m_look.password) {
      // Password mask: one bullet advance per character, drawn as a small dot.
      const auto bullet = renderer.measureText("•", m_look.fontPx, FontWeight::Normal, 0.0F, 1, TextAlign::Start,
                                               kusanagi::font());
      const float dot = bullet.inkRight - bullet.inkLeft;
      m_input->setPasswordMaskMetrics(std::round(bullet.width), dot > 0.0F ? dot * 1.25F : m_look.fontPx * 0.36F);
    }
    // Input keeps its text 3 px in from its edges. Widen it so the text starts at `left`.
    constexpr float kInset = 3.0F;
    m_input->setSize(iw + 2.0F * kInset, h);
    m_input->setPosition(left - kInset, 0.0F);
    m_input->layout(renderer);
    m_placeholder->setMaxWidth(iw);
    m_placeholder->measure(renderer);
    m_placeholder->setPosition(left, std::round((h - m_placeholder->height()) / 2.0F));
    syncPlaceholder();
  }

} // namespace kusanagi::cp
