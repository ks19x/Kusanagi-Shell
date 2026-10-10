#pragma once

// Power menu, panel id "session": lock, log out, suspend, reboot and shut down over a dimmed screen.
// power.style picks the layout (row, tiles, list, fullscreen or pill). Log out, reboot and shut down need a
// second click within 3 s. The arrow keys and Tab move, Enter and Space pick, and L E S R P pick directly.

#include "core/timer_manager.h"
#include "render/animation/animation_manager.h"
#include "render/core/color.h"
#include "shell/panel/panel.h"

#include <array>
#include <functional>
#include <string>

class Box;
class InputArea;
class Label;
class SessionActionRunner;

class KusanagiPowerPanel : public Panel {
public:
  explicit KusanagiPowerPanel(SessionActionRunner& runner) : m_runner(&runner) {}

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
  bool beginCloseAnimation(std::function<void()> done) override;

private:
  struct Item {
    char32_t icon;
    const char* label;
    char hint;
    bool confirm;
    const char* action; // SessionActionRunner action
  };
  struct Button {
    InputArea* area = nullptr;
    Box* face = nullptr;
    Label* icon = nullptr;
    Label* label = nullptr;
    Label* hint = nullptr;
    // Animated face colour and scale.
    Color faceShown;
    Color faceTarget;
    bool faceSet = false;
    AnimationManager::Id faceAnim = 0;
    float scale = 1.0F;
    float scaleTarget = 1.0F;
    AnimationManager::Id scaleAnim = 0;
    bool hovered = false;
  };

  void doLayout(Renderer& renderer, float width, float height) override;
  void pick(std::size_t i);
  void step(int d);
  void animateTo(float target, std::function<void()> done = {});
  void animateFace(Button& b, const Color& target, float durationMs);
  void animateScale(Button& b, float target, bool spring);

  SessionActionRunner* m_runner = nullptr;
  std::string m_style = "row";
  std::size_t m_sel = 0;
  int m_armed = -1;
  Timer m_disarm;
  float m_open = 0.0F;      // opacity; the scale follows with its own spring
  float m_scaleT = 0.0F;    // eased 0..1, may overshoot
  bool m_closing = false;
  AnimationManager::Id m_anim = 0;
  AnimationManager::Id m_animScale = 0;

  Box* m_backdrop = nullptr;
  InputArea* m_backdropArea = nullptr;
  Box* m_shadow = nullptr;
  Box* m_card = nullptr;
  Node* m_cardNode = nullptr;
  Label* m_title = nullptr; // pill style: the current or armed action above the pill
  std::array<Button, 5> m_buttons{};
  float m_width = 0.0F;
  float m_height = 0.0F;
};
