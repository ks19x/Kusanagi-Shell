#include "shell/kusanagi/polkit_panel.h"

#include "cursor-shape-v1-client-protocol.h"
#include "dbus/polkit/polkit_agent.h"
#include "i18n/i18n.h"
#include "render/animation/animation_manager.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "shell/kusanagi/dev_prompts.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/panel/cp_controls.h"
#include "shell/kusanagi/panel/cp_field.h"
#include "shell/panel/panel_manager.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <cmath>
#include <regex>

namespace kusanagi {

  namespace {

    constexpr float kCardW = 420.0F;
    constexpr float kPad = 24.0F;
    constexpr float kSpacing = 14.0F;

    // The PolkitAgent as a flow.
    class AgentFlow : public PolkitFlow {
    public:
      explicit AgentFlow(std::function<PolkitAgent*()> agent) : m_agent(std::move(agent)) {}

      [[nodiscard]] bool active() const override {
        PolkitAgent* a = get();
        return a != nullptr && a->hasPendingRequest();
      }
      [[nodiscard]] std::string actionId() const override { return get() ? get()->pendingRequest().actionId : ""; }
      [[nodiscard]] std::string message() const override { return get() ? get()->pendingRequest().message : ""; }
      [[nodiscard]] std::vector<std::string> identities() const override {
        std::vector<std::string> out;
        if (get() == nullptr) return out;
        for (const auto& id : get()->pendingRequest().identities) {
          if (id.kind == "unix-group") {
            out.push_back("group " + std::to_string(id.uid));
          } else {
            out.push_back(!id.displayName.empty() ? id.displayName : id.userName);
          }
        }
        return out;
      }
      [[nodiscard]] std::size_t selectedIdentity() const override { return get() ? get()->selectedIdentity() : 0; }
      void selectIdentity(std::size_t index) override {
        if (get() != nullptr) get()->selectIdentity(index);
      }
      [[nodiscard]] bool responseRequired() const override { return get() && get()->isResponseRequired(); }
      [[nodiscard]] bool responseVisible() const override { return get() && get()->responseVisible(); }
      [[nodiscard]] std::string inputPrompt() const override { return get() ? get()->inputPrompt() : ""; }
      [[nodiscard]] std::string supplementaryMessage() const override {
        if (get() == nullptr) return "";
        // Hide the agent's own "authenticating" and "wrong password" lines: the dialog says so in the field
        // and shakes the card instead. PAM's messages (faillock and such) still show.
        std::string m = get()->supplementaryMessage();
        if (m == i18n::tr("auth.polkit.authenticating") || m == i18n::tr("auth.polkit.invalid-password")) return "";
        return m;
      }
      [[nodiscard]] bool supplementaryIsError() const override { return get() && get()->supplementaryIsError(); }
      [[nodiscard]] std::uint32_t failures() const override { return get() ? get()->failureCount() : 0; }
      void submit(const std::string& response) override {
        if (get() != nullptr) get()->submitResponse(response);
      }
      void cancel() override {
        if (get() != nullptr && get()->hasPendingRequest()) get()->cancelRequest();
      }

    private:
      [[nodiscard]] PolkitAgent* get() const { return m_agent ? m_agent() : nullptr; }
      std::function<PolkitAgent*()> m_agent;
    };

    std::string escapeMarkup(const std::string& s) {
      std::string out;
      for (const char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        default: out += c;
        }
      }
      return out;
    }

    // Wrapped text with a line pitch of round(ascent) + round(descent). Pango rounds both up, which would
    // make a two-line message 2 px taller than the classic card.
    void setWrapped(Renderer& renderer, Label& label, const std::string& text, float px) {
      const auto fm = renderer.measureFont(px, FontWeight::Normal, kusanagi::font());
      const float pitch = std::round(-fm.top) + std::round(fm.bottom);
      label.setUseMarkup(true);
      label.setText("<span line_height=\"" + std::to_string(std::lround(pitch * 1024.0F * renderer.renderScale())) + "\">"
                    + escapeMarkup(text) + "</span>");
    }

    // OutBack easing with overshoot `s`.
    float outBack(float t, float s) {
      const float f = t - 1.0F;
      return 1.0F + (s + 1.0F) * f * f * f + s * f * f;
    }

  } // namespace

  KusanagiPolkitPanel::KusanagiPolkitPanel(std::function<PolkitAgent*()> agent)
      : m_agentFlow(std::make_unique<AgentFlow>(std::move(agent))) {}

  KusanagiPolkitPanel::~KusanagiPolkitPanel() = default;

  PolkitFlow* KusanagiPolkitPanel::flow() const {
    if (PolkitFlow* fake = dev::polkitFlow(); fake != nullptr && fake->active()) return fake;
    return m_agentFlow.get();
  }

  void KusanagiPolkitPanel::create() {
    auto root = ui::node({});
    // The dimmed backdrop swallows clicks.
    auto backdrop = ui::inputArea({});
    m_backdrop = static_cast<Box*>(backdrop->addChild(ui::box({})));
    root->addChild(std::move(backdrop));

    auto card = ui::inputArea({});
    m_cardNode = card.get();
    m_card = static_cast<Box*>(card->addChild(ui::box({})));

    m_iconBg = static_cast<Box*>(card->addChild(ui::box({})));
    m_icon = static_cast<Label*>(card->addChild(cp::icon(0xf0483, 22.0F, cp::accent())));
    m_title = static_cast<Label*>(card->addChild(cp::text("Authentication required", 15.0F, true)));
    m_action = static_cast<Label*>(card->addChild(cp::text("", 10.0F, false, cp::dim())));
    m_action->setMaxLines(1);
    m_action->setEllipsize(TextEllipsize::Middle);
    m_message = static_cast<Label*>(card->addChild(cp::text("", 12.0F, false, cp::textA(0.85F))));

    // Who authenticates. With several admins, a click switches.
    auto who = ui::inputArea({});
    m_who = who.get();
    m_whoBg = static_cast<Box*>(who->addChild(ui::box({})));
    m_whoIcon = static_cast<Label*>(who->addChild(cp::icon(0xf0004, 15.0F, cp::dim())));
    m_whoName = static_cast<Label*>(who->addChild(cp::text("you", 12.0F)));
    m_whoName->setMaxLines(1);
    m_whoSwitch = static_cast<Label*>(who->addChild(cp::text("switch ⇄", 11.0F, false, cp::accent())));
    who->setOnClick([this](const InputArea::PointerData&) {
      PolkitFlow* f = flow();
      if (f == nullptr) return;
      const auto ids = f->identities();
      if (ids.size() < 2) return;
      f->selectIdentity((f->selectedIdentity() + 1) % ids.size());
      PanelManager::instance().refresh();
    });
    card->addChild(std::move(who));

    // Password
    cp::Field::Look look{.height = 42.0F, .radius = 12.0F, .fontPx = 14.0F, .placeholderPx = 13.0F,
                         .idleBorderAlpha = 0.1F, .placeholderWhileFocused = true, .password = true,
                         .animateBorder = false};
    auto field = std::make_unique<cp::Field>(look);
    field->setOnAccepted([this](const std::string&) { submit(); });
    m_field = static_cast<cp::Field*>(card->addChild(std::move(field)));

    m_supplementary = static_cast<Label*>(card->addChild(cp::text("", 11.0F, false, cp::dim())));

    auto cancel = std::make_unique<cp::Chip>("Cancel");
    cancel->setOnActivate([this]() { this->cancel(); });
    m_cancel = static_cast<cp::Chip*>(card->addChild(std::move(cancel)));
    auto ok = std::make_unique<cp::Chip>("Authenticate", 0xf0483, true);
    ok->setOnActivate([this]() { submit(); });
    m_ok = static_cast<cp::Chip*>(card->addChild(std::move(ok)));

    root->addChild(std::move(card));
    setRoot(std::move(root));
  }

  void KusanagiPolkitPanel::onOpen(std::string_view /*context*/) {
    PolkitFlow* f = flow();
    m_failures = f != nullptr ? f->failures() : 0;
    m_lastRequired = f != nullptr && f->responseRequired();
    m_shakeX = 0.0F;
    m_shown = 0.0F;
    m_scale = 0.94F;
    if (m_animations == nullptr) {
      m_shown = 1.0F;
      m_scale = 1.0F;
      return;
    }
    // Fade in over 200 ms, scale in over 340 ms with a slight overshoot.
    m_animations->animate(0.0F, 1.0F, 200.0F, Easing::Linear, [this](float v) {
      m_shown = v;
      PanelManager::instance().requestLayout();
    }, {}, this);
    const float overshoot = kusanagi::bounce(1.2F);
    m_animations->animate(0.0F, 1.0F, 340.0F, Easing::Linear, [this, overshoot](float t) {
      m_scale = 0.94F + 0.06F * outBack(t, overshoot);
      PanelManager::instance().requestLayout();
    }, {}, this);
  }

  void KusanagiPolkitPanel::onClose() {
    if (m_animations != nullptr) m_animations->cancelForOwner(this);
    // Closing the dialog (Escape, or another panel replacing it) cancels the request.
    if (PolkitFlow* f = flow(); f != nullptr && f->active()) f->cancel();
    m_backdrop = nullptr;
    m_cardNode = nullptr;
    m_card = nullptr;
    m_field = nullptr;
    m_who = nullptr;
  }

  InputArea* KusanagiPolkitPanel::initialFocusArea() const {
    return m_field != nullptr ? m_field->focusArea() : nullptr;
  }

  bool KusanagiPolkitPanel::handleGlobalKey(std::uint32_t sym, std::uint32_t /*modifiers*/, bool pressed, bool preedit) {
    if (!pressed || preedit || sym != XKB_KEY_Escape) return false;
    cancel();
    return true;
  }

  void KusanagiPolkitPanel::submit() {
    PolkitFlow* f = flow();
    if (f == nullptr || m_field == nullptr || !f->responseRequired()) return;
    const std::string text = m_field->text();
    m_field->setText("");
    f->submit(text);
    PanelManager::instance().refresh();
  }

  void KusanagiPolkitPanel::cancel() {
    if (PolkitFlow* f = flow(); f != nullptr) f->cancel();
    PanelManager::instance().closePanel();
  }

  void KusanagiPolkitPanel::shake() {
    // A fixed shake, not scaled by look.animSpeed.
    if (m_animations == nullptr) return;
    struct Step {
      float to;
      float ms;
    };
    static constexpr Step steps[] = {{-10.0F, 50.0F}, {10.0F, 70.0F}, {-6.0F, 60.0F}, {0.0F, 50.0F}};
    auto run = std::make_shared<std::function<void(std::size_t)>>();
    *run = [this, run](std::size_t i) {
      if (i >= std::size(steps)) return;
      const float from = m_shakeX;
      m_animations->animateTimer(from, steps[i].to, steps[i].ms, Easing::Linear, [this](float v) {
        m_shakeX = v;
        PanelManager::instance().requestLayout();
      }, [run, i]() { (*run)(i + 1); }, this);
    };
    (*run)(0);
  }

  void KusanagiPolkitPanel::doUpdate(Renderer& /*renderer*/) {
    PolkitFlow* f = flow();
    if (f == nullptr || m_field == nullptr) return;
    bool changed = false;
    changed = m_action->setText(f->actionId()) || changed;
    if (f->message() != m_messageText) {
      m_messageText = f->message();
      changed = true;
    }
    const auto ids = f->identities();
    const std::size_t sel = f->selectedIdentity();
    std::string who = sel < ids.size() ? ids[sel] : "";
    changed = m_whoName->setText(who.empty() ? "you" : who) || changed;
    if (m_whoSwitch->visible() != (ids.size() > 1)) changed = true;
    m_whoSwitch->setVisible(ids.size() > 1);
    m_who->setCursorShape(ids.size() > 1 ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);

    const bool required = f->responseRequired();
    std::string prompt = f->inputPrompt().empty() ? "Password" : f->inputPrompt();
    prompt = std::regex_replace(prompt, std::regex(":\\s*$"), "");
    m_field->setPlaceholder(required ? prompt : "Checking…");
    m_field->setEnabled(required);
    m_field->setPasswordRevealed(f->responseVisible());

    const std::string sup = f->supplementaryMessage();
    if (sup != m_supText) {
      m_supText = sup;
      changed = true;
    }
    if (m_supplementary->visible() != !sup.empty()) changed = true;
    m_supplementary->setVisible(!sup.empty());
    m_supplementary->setColor(f->supplementaryIsError() ? cp::danger() : cp::dim());

    // A wrong password: shake, then back to typing.
    if (f->failures() != m_failures) {
      m_failures = f->failures();
      shake();
    }
    if (required && !m_lastRequired) PanelManager::instance().focusArea(m_field->focusArea());
    m_lastRequired = required;
    if (changed) PanelManager::instance().requestLayout();
  }

  void KusanagiPolkitPanel::doLayout(Renderer& renderer, float width, float height) {
    Node* rootNode = root();
    if (rootNode == nullptr || m_card == nullptr) return;
    rootNode->setSize(width, height);

    auto* backdropArea = m_backdrop->parent();
    backdropArea->setPosition(0.0F, 0.0F);
    backdropArea->setSize(width, height);
    m_backdrop->setSize(width, height);
    m_backdrop->setFill(ColorSpec{.role = std::nullopt, .fixed = rgba(0.0F, 0.0F, 0.0F, 1.0F), .alpha = 0.45F * m_shown});

    const float bw = kCardW - 2.0F * kPad;
    float y = kPad;

    // Header: the shield icon, then the title over the action id
    m_title->measure(renderer);
    m_action->setMaxWidth(bw - 58.0F);
    m_action->measure(renderer);
    const float colH = m_title->height() + 2.0F + m_action->height();
    const float rowH = std::max(44.0F, colH);
    m_iconBg->setPosition(kPad, y + std::round((rowH - 44.0F) / 2.0F));
    m_iconBg->setSize(44.0F, 44.0F);
    m_iconBg->setRadius(14.0F);
    m_iconBg->setFill(cp::accent(0.16F));
    m_icon->measure(renderer);
    cp::centerIn(*m_icon, m_iconBg->x(), m_iconBg->y(), 44.0F, 44.0F);
    const float colY = y + std::round((rowH - colH) / 2.0F);
    m_title->setPosition(kPad + 44.0F + 14.0F, colY);
    m_action->setPosition(kPad + 44.0F + 14.0F, colY + m_title->height() + 2.0F);
    y += rowH + kSpacing;

    setWrapped(renderer, *m_message, m_messageText, 12.0F);
    m_message->setMaxWidth(bw);
    m_message->measure(renderer);
    m_message->setPosition(kPad, y);
    y += m_message->height() + kSpacing;

    m_who->setPosition(kPad, y);
    m_who->setSize(bw, 36.0F);
    m_whoBg->setSize(bw, 36.0F);
    m_whoBg->setRadius(10.0F);
    m_whoBg->setFill(cp::textA(0.05F));
    m_whoIcon->measure(renderer);
    m_whoIcon->setPosition(12.0F, std::round((36.0F - m_whoIcon->height()) / 2.0F));
    m_whoSwitch->measure(renderer);
    m_whoSwitch->setPosition(bw - 12.0F - m_whoSwitch->width(), std::round((36.0F - m_whoSwitch->height()) / 2.0F));
    m_whoName->setMaxWidth(bw - 36.0F - 12.0F);
    m_whoName->measure(renderer);
    m_whoName->setPosition(36.0F, std::round((36.0F - m_whoName->height()) / 2.0F));
    y += 36.0F + kSpacing;

    m_field->setPosition(kPad, y);
    m_field->setSize(bw, 42.0F);
    m_field->layout(renderer);
    y += 42.0F + kSpacing;

    if (m_supplementary->visible()) {
      setWrapped(renderer, *m_supplementary, m_supText, 11.0F);
      m_supplementary->setMaxWidth(bw);
      m_supplementary->measure(renderer);
      m_supplementary->setPosition(kPad, y);
      y += m_supplementary->height() + kSpacing;
    }

    // Buttons, right-aligned
    m_cancel->layout(renderer);
    m_ok->layout(renderer);
    const float okX = kPad + bw - m_ok->width();
    m_ok->setPosition(okX, y);
    m_cancel->setPosition(okX - 8.0F - m_cancel->width(), y);
    y += std::max(m_ok->height(), m_cancel->height());

    const float ch = y + kPad;
    m_card->setSize(kCardW, ch);
    m_card->setRadius(kusanagi::radius());
    m_card->setFill(cp::bgPanel(std::max(0.92F, static_cast<float>(kusanagi::opt<double>("panel", "opacity", 0.95)))));
    m_card->setBorder(kusanagi::surfaceBorder(), kusanagi::surfaceBorderWidth());

    const float cx = std::round((width - kCardW) / 2.0F) + m_shakeX;
    const float cy = std::round((height - ch) / 2.0F);
    m_cardNode->setPosition(cx, cy);
    m_cardNode->setSize(kCardW, ch);
    m_cardNode->setTransformOrigin(kCardW / 2.0F, ch / 2.0F);
    m_cardNode->setScale(m_scale);
    m_cardNode->setOpacity(m_shown);
  }

} // namespace kusanagi
