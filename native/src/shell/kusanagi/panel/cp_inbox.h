#pragma once

// Control panel inbox: the notification history (newest first, in the same cards as the popups),
// do-not-disturb and clear-all.

#include "shell/kusanagi/panel/cp_page.h"
#include "system/icon_resolver.h"

#include <cstdint>
#include <memory>
#include <vector>

class Label;

namespace kusanagi {

  class NotificationCard;
  namespace cp {
    class Chip;
    class Switch;
  } // namespace cp

  class CpInbox : public CpPage {
  public:
    CpInbox(ControlPanel& panel, const ControlCenterServices& services);
    ~CpInbox() override;

    bool sync(Renderer& renderer) override;
    float layout(Renderer& renderer, float width) override;
    void tick() override;

  private:
    void rebuildCards(Renderer& renderer, float width);
    void scrollBy(float dy);

    Label* m_count = nullptr;
    Label* m_dndLabel = nullptr;
    cp::Switch* m_dnd = nullptr;
    cp::Chip* m_clear = nullptr;
    Node* m_listClip = nullptr;
    Node* m_list = nullptr;
    Node* m_empty = nullptr;
    Label* m_emptyIcon = nullptr;
    Label* m_emptyText = nullptr;
    std::vector<NotificationCard*> m_cards;
    std::uint64_t m_serial = ~std::uint64_t{0};
    std::uint64_t m_builtSerial = ~std::uint64_t{0};
    float m_builtWidth = 0.0F;
    float m_listH = 0.0F;
    float m_contentH = 0.0F;
    float m_scroll = 0.0F;
    int m_countValue = -1;
    IconResolver m_icons;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
  };

} // namespace kusanagi
