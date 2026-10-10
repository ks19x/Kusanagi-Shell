#include "shell/kusanagi/panel/cp_inbox.h"

#include "core/deferred_call.h"
#include "net/uri.h"
#include "notification/notification_manager.h"
#include "render/core/renderer.h"
#include "shell/control_center/control_center_services.h"
#include "shell/kusanagi/panel/control_panel.h"
#include "shell/kusanagi/panel/cp_controls.h"
#include "shell/notification/kusanagi_notification_card.h"
#include "shell/panel/panel_manager.h"
#include "ui/builders.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <cmath>
#include <unistd.h>
#include <wayland-client-protocol.h>

namespace kusanagi {

  namespace {
    constexpr float kMaxListH = 470.0F;
    constexpr std::size_t kMaxCards = 100;
  } // namespace

  CpInbox::CpInbox(ControlPanel& panel, const ControlCenterServices& services) : CpPage(panel, services) {
    m_count = static_cast<Label*>(addChild(cp::text("", 13.0F, true)));
    m_dndLabel = static_cast<Label*>(addChild(cp::text("Do not disturb", 11.0F, false, cp::dim())));
    auto sw = std::make_unique<cp::Switch>();
    sw->setOnToggled([this](bool on) {
      if (m_services.notifications != nullptr) m_services.notifications->setDoNotDisturb(on);
      m_dnd->setOn(on, true);
    });
    m_dnd = static_cast<cp::Switch*>(addChild(std::move(sw)));
    auto clear = std::make_unique<cp::Chip>("Clear all", 0xf0a7a);
    clear->setOnActivate([this]() {
      DeferredCall::callLater([this, alive = std::weak_ptr<bool>(m_alive)]() {
        if (alive.expired() || m_services.notifications == nullptr) return;
        m_services.notifications->clearHistory();
        PanelManager::instance().refresh();
      });
    });
    m_clear = static_cast<cp::Chip*>(addChild(std::move(clear)));

    m_listClip = addChild(ui::inputArea({
        .clipChildren = true,
        .onAxisHandler =
            [this](const InputArea::PointerData& d) {
              if (d.axis != WL_POINTER_AXIS_VERTICAL_SCROLL || m_contentH <= m_listH) return false;
              scrollBy(d.scrollDelta(48.0F));
              return true;
            },
    }));
    m_list = m_listClip->addChild(ui::node({}));

    m_empty = addChild(ui::node({}));
    m_emptyIcon = static_cast<Label*>(m_empty->addChild(cp::icon(0xf11e5, 34.0F, cp::textA(0.25F))));
    m_emptyText = static_cast<Label*>(m_empty->addChild(cp::text("All caught up", 12.0F, false, cp::dim())));
  }

  CpInbox::~CpInbox() { *m_alive = false; }

  void CpInbox::scrollBy(float dy) {
    m_scroll = std::clamp(m_scroll + dy, 0.0F, std::max(0.0F, m_contentH - m_listH));
    m_list->setPosition(0.0F, -std::round(m_scroll));
  }

  bool CpInbox::sync(Renderer& /*renderer*/) {
    NotificationManager* nm = m_services.notifications;
    bool changed = false;
    const int count = nm != nullptr ? static_cast<int>(nm->history().size()) : 0;
    if (count != m_countValue) {
      m_countValue = count;
      m_count->setText(count > 0 ? std::to_string(count) + " notification" + (count == 1 ? "" : "s") : "No notifications");
      changed = true;
    }
    m_dnd->setOn(nm != nullptr && nm->doNotDisturb(), true);
    const std::uint64_t serial = nm != nullptr ? nm->changeSerial() : 0;
    if (serial != m_serial) {
      m_serial = serial;
      changed = true;
    }
    return changed;
  }

  void CpInbox::tick() {
    if (m_services.notifications == nullptr) return;
    for (NotificationCard* card : m_cards) {
      for (const auto& e : m_services.notifications->history()) {
        if (e.notification.id == card->notificationId()) {
          card->setAge(notificationAge(e.notification));
          break;
        }
      }
    }
  }

  void CpInbox::rebuildCards(Renderer& renderer, float width) {
    for (NotificationCard* card : m_cards) (void)m_list->removeChild(card);
    m_cards.clear();
    NotificationManager* nm = m_services.notifications;
    if (nm == nullptr) return;
    NotificationCardStyle style = NotificationCardStyle::fromSettings(false);
    if (style.style == "minimal") style.style = "comfortable";  // minimal is a popup-only look
    style.width = width;
    const auto& history = nm->history();
    // Newest first. Entries beyond kMaxCards stay in the history but aren't drawn.
    std::size_t shown = 0;
    for (auto it = history.rbegin(); it != history.rend() && shown < kMaxCards; ++it, ++shown) {
      const Notification& n = it->notification;
      const std::uint32_t id = n.id;
      NotificationCardData data = notificationCardData(n, m_icons);
      const std::weak_ptr<bool> alive = m_alive;
      NotificationCardCallbacks callbacks{
          .onActivate =
              [this, id, alive]() {
                DeferredCall::callLater([this, id, alive]() {
                  if (alive.expired() || m_services.notifications == nullptr) return;
                  NotificationManager* manager = m_services.notifications;
                  bool hasDefault = false;
                  for (const auto& e : manager->history()) {
                    if (e.notification.id != id) continue;
                    for (std::size_t i = 0; i + 1 < e.notification.actions.size(); i += 2) {
                      hasDefault = hasDefault || e.notification.actions[i] == "default";
                    }
                  }
                  if (hasDefault) (void)manager->invokeAction(id, "default", true);
                  manager->removeHistoryEntry(id);
                  m_panel.close();
                });
              },
          .onDismissed =
              [this, id, alive]() {
                DeferredCall::callLater([this, id, alive]() {
                  if (alive.expired() || m_services.notifications == nullptr) return;
                  m_services.notifications->removeHistoryEntry(id);
                  PanelManager::instance().refresh();
                });
              },
          .onAction =
              [this, id, alive](const std::string& key) {
                DeferredCall::callLater([this, id, key, alive]() {
                  if (alive.expired() || m_services.notifications == nullptr) return;
                  (void)m_services.notifications->invokeAction(id, key, true);
                  m_services.notifications->removeHistoryEntry(id);
                  PanelManager::instance().refresh();
                });
              },
      };
      auto card = std::make_unique<NotificationCard>(renderer, animationManager(), std::move(data), style,
                                                     std::move(callbacks));
      m_cards.push_back(static_cast<NotificationCard*>(m_list->addChild(std::move(card))));
    }
  }

  float CpInbox::layout(Renderer& renderer, float width) {
    // Header: count on the left, do-not-disturb and "clear all" on the right.
    const float hh = 30.0F;
    m_count->measure(renderer);
    m_count->setPosition(0.0F, std::round((hh - m_count->height()) / 2.0F));
    const bool any = m_countValue > 0;
    m_clear->setVisible(any);
    float x = width;
    if (any) {
      m_clear->layout(renderer);
      x -= m_clear->width();
      m_clear->setPosition(x, std::round((hh - m_clear->height()) / 2.0F));
      x -= 10.0F;
    }
    x -= m_dnd->width();
    m_dnd->setPosition(x, std::round((hh - m_dnd->height()) / 2.0F));
    x -= 10.0F;
    m_dndLabel->measure(renderer);
    m_dndLabel->setPosition(x - m_dndLabel->width(), std::round((hh - m_dndLabel->height()) / 2.0F));

    float y = hh + 12.0F;
    m_listClip->setVisible(any);
    m_empty->setVisible(!any);
    if (any) {
      if (m_builtSerial != m_serial || m_builtWidth != width) {
        m_builtSerial = m_serial;
        m_builtWidth = width;
        rebuildCards(renderer, width);
      }
      float cy = 0.0F;
      for (NotificationCard* card : m_cards) {
        card->setPosition(0.0F, cy);
        cy += card->cardHeight() + 8.0F;
      }
      m_contentH = std::max(0.0F, cy - 8.0F);
      m_listH = std::min(m_contentH, kMaxListH);
      m_listClip->setPosition(0.0F, y);
      m_listClip->setSize(width, m_listH);
      m_list->setSize(width, m_contentH);
      scrollBy(0.0F);
      return y + m_listH;
    }

    // Empty state
    m_emptyIcon->measure(renderer);
    m_emptyText->measure(renderer);
    m_empty->setPosition(0.0F, y);
    m_emptyIcon->setPosition(std::round((width - m_emptyIcon->width()) / 2.0F), 24.0F);
    m_emptyText->setPosition(std::round((width - m_emptyText->width()) / 2.0F), 24.0F + m_emptyIcon->height() + 8.0F);
    const float eh = 24.0F + m_emptyIcon->height() + 8.0F + m_emptyText->height() + 28.0F;
    m_empty->setSize(width, eh);
    return y + eh;
  }

} // namespace kusanagi
