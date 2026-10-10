#pragma once

// Polkit authentication dialog, registered as the "polkit" panel and opened by the PolkitAgent
// when an app needs admin rights. A wrong password shakes the card. Enter submits, Escape cancels, and clicks
// outside do nothing. polkit.enabled decides whether the agent runs at all.

#include "shell/panel/panel.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Box;
class InputArea;
class Label;
class PolkitAgent;
struct PolkitRequest;

namespace kusanagi {

  namespace cp {
    class Chip;
    class Field;
  } // namespace cp

  // What the dialog shows and answers: the real agent, or a fake from dev_prompts.h so test shells can
  // show it without a real request.
  class PolkitFlow {
  public:
    virtual ~PolkitFlow() = default;
    [[nodiscard]] virtual bool active() const = 0;
    [[nodiscard]] virtual std::string actionId() const = 0;
    [[nodiscard]] virtual std::string message() const = 0;
    [[nodiscard]] virtual std::vector<std::string> identities() const = 0;  // display names
    [[nodiscard]] virtual std::size_t selectedIdentity() const = 0;
    virtual void selectIdentity(std::size_t index) = 0;
    [[nodiscard]] virtual bool responseRequired() const = 0;
    [[nodiscard]] virtual bool responseVisible() const = 0;
    [[nodiscard]] virtual std::string inputPrompt() const = 0;
    [[nodiscard]] virtual std::string supplementaryMessage() const = 0;
    [[nodiscard]] virtual bool supplementaryIsError() const = 0;
    [[nodiscard]] virtual std::uint32_t failures() const = 0;  // wrong answers so far
    virtual void submit(const std::string& response) = 0;
    virtual void cancel() = 0;
  };

  class KusanagiPolkitPanel : public Panel {
  public:
    explicit KusanagiPolkitPanel(std::function<PolkitAgent*()> agent);
    ~KusanagiPolkitPanel() override;

    void create() override;
    void onOpen(std::string_view context) override;
    void onClose() override;
    [[nodiscard]] bool handleGlobalKey(std::uint32_t sym, std::uint32_t modifiers, bool pressed, bool preedit) override;
    [[nodiscard]] InputArea* initialFocusArea() const override;

    [[nodiscard]] float preferredWidth() const override { return 0.0F; }
    [[nodiscard]] float preferredHeight() const override { return 0.0F; }
    [[nodiscard]] bool coversOutput() const noexcept override { return true; }
    [[nodiscard]] bool dismissOnOutsideClick() const override { return false; }
    [[nodiscard]] bool hasDecoration() const override { return false; }
    [[nodiscard]] LayerShellKeyboard keyboardMode() const override { return LayerShellKeyboard::Exclusive; }
    [[nodiscard]] LayerShellLayer layer() const override { return LayerShellLayer::Overlay; }
    // The dialog just goes away when the flow ends.
    [[nodiscard]] bool wantsCloseAnimation() const noexcept override { return false; }

  private:
    void doLayout(Renderer& renderer, float width, float height) override;
    void doUpdate(Renderer& renderer) override;
    [[nodiscard]] PolkitFlow* flow() const;
    void submit();
    void cancel();
    void shake();

    std::unique_ptr<PolkitFlow> m_agentFlow;

    Box* m_backdrop = nullptr;
    Node* m_cardNode = nullptr;
    Box* m_card = nullptr;
    Box* m_iconBg = nullptr;
    Label* m_icon = nullptr;
    Label* m_title = nullptr;
    Label* m_action = nullptr;
    Label* m_message = nullptr;
    InputArea* m_who = nullptr;
    Box* m_whoBg = nullptr;
    Label* m_whoIcon = nullptr;
    Label* m_whoName = nullptr;
    Label* m_whoSwitch = nullptr;
    cp::Field* m_field = nullptr;
    Label* m_supplementary = nullptr;
    cp::Chip* m_cancel = nullptr;
    cp::Chip* m_ok = nullptr;

    std::string m_messageText;
    std::string m_supText;
    float m_shown = 0.0F;     // opacity
    float m_scale = 0.94F;
    float m_shakeX = 0.0F;    // horizontal offset while shaking
    std::uint32_t m_failures = 0;
    bool m_lastRequired = false;
  };

} // namespace kusanagi
