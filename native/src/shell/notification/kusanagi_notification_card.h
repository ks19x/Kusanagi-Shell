#pragma once

// Kusanagi's notification card, shared by the popups and the control panel's Inbox. The card is
// style.width by cardHeight(); the owner positions it. A right click, the close button or a swipe right
// flies it off and then calls onDismissed. Popups pause their countdown while onHoverChanged says hovered.

#include "notification/notification.h"
#include "render/animation/animation_manager.h"
#include "render/scene/input_area.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Box;
class IconResolver;
class Input;
class Label;
class Node;
class Renderer;

namespace kusanagi {

  struct NotificationCardData {
    std::uint32_t id = 0;
    std::string appName;
    std::string summary;
    std::string body;                 // plain text (the notification server strips markup at D-Bus ingress)
    std::string bodyMarkup;           // the body as sent (styled text), or empty
    std::vector<std::string> actions; // pairs: key, label, ... ("default" is the click, not a chip)
    std::string appIconPath;          // resolved app icon file, or empty
    std::string appGlyph;             // Tabler glyph name for internal notifications ("kusanagi-glyph:...")
    const NotificationImageData* image = nullptr; // big picture (album art, avatar, ...)
    std::string imagePath;            // or a picture file
    bool critical = false;
    std::string age;                  // "now", "5m", "2h"; see notificationAge()
    bool replyMode = false;           // inline reply field instead of the chips
  };

  struct NotificationCardStyle {
    std::string style = "comfortable"; // comfortable, compact, minimal (popups only) or accent
    bool images = true;
    bool progress = true;
    bool popup = true; // popups: panel background, shadow, countdown; Inbox: a quiet row
    float width = 400.0F;

    // The notifications settings as of the last settings load.
    [[nodiscard]] static NotificationCardStyle fromSettings(bool popup);
  };

  struct NotificationCardCallbacks {
    std::function<void()> onActivate;                    // left click
    std::function<void()> onDismissed;                   // after the fly-off
    std::function<void(const std::string&)> onAction;    // an action chip (not "inline-reply")
    std::function<void()> onReplyRequested;              // the "inline-reply" chip
    std::function<void(const std::string&)> onReply;     // reply submitted
    std::function<void(bool)> onReplyFocusChanged;
    std::function<void(bool)> onHoverChanged;
  };

  // "now" under a minute, then 5m, 3h, 2d.
  [[nodiscard]] std::string notificationAge(const Notification& n);

  // A card's content from a notification: display app name, resolved app icon or Tabler glyph,
  // picture, age. data.image points into n.imageData, so `n` must outlive building the card.
  [[nodiscard]] NotificationCardData notificationCardData(const Notification& n, IconResolver& icons);

  class NotificationCard : public InputArea {
  public:
    NotificationCard(
        Renderer& renderer, AnimationManager* animations, NotificationCardData data, NotificationCardStyle style,
        NotificationCardCallbacks callbacks
    );
    ~NotificationCard() override;

    [[nodiscard]] float cardHeight() const noexcept { return m_height; }
    [[nodiscard]] bool hovered() const noexcept { return m_hovered; }
    [[nodiscard]] bool leaving() const noexcept { return m_leaving; }
    [[nodiscard]] std::uint32_t notificationId() const noexcept { return m_data.id; }
    [[nodiscard]] Input* replyInput() const noexcept { return m_replyInput; }

    // The countdown hairline, from 1 down to 0.
    void setRemaining(float remaining);
    void setAge(const std::string& age);
    // Flies off to the right, then calls onDismissed.
    void dismiss();

  private:
    void build(Renderer& renderer);
    void buildMinimal(Renderer& renderer);
    void buildFull(Renderer& renderer);
    void setSwipe(float x);
    void springBack();
    void hoverRef(int delta);
    void applyHover();
    [[nodiscard]] float surfaceRadius() const;

    AnimationManager* m_animations = nullptr;
    NotificationCardData m_data;
    NotificationCardStyle m_style;
    NotificationCardCallbacks m_callbacks;

    Node* m_surface = nullptr;
    Box* m_shadow = nullptr;
    Box* m_background = nullptr;
    Box* m_hairline = nullptr;
    Node* m_close = nullptr;
    Box* m_closeBg = nullptr;
    Label* m_closeGlyph = nullptr;
    Label* m_age = nullptr;
    Input* m_replyInput = nullptr;

    float m_height = 0.0F;
    float m_hairlineX = 0.0F;
    float m_hairlineWidth = 0.0F;
    float m_swipe = 0.0F;
    float m_pressX = 0.0F;
    bool m_leftDown = false;
    bool m_dragging = false;
    bool m_dragged = false;
    bool m_leaving = false;
    bool m_hovered = false;
    int m_hoverRefs = 0;
    bool m_hoverPending = false;
    AnimationManager::Id m_swipeAnim = 0;
    AnimationManager::Id m_fadeAnim = 0;
    AnimationManager::Id m_closeAnim = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
  };

} // namespace kusanagi
