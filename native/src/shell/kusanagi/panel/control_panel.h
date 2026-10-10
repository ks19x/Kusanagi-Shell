#pragma once

// Kusanagi's control panel, registered as the "control-center" panel. It covers the whole output and draws
// its own backdrop, shadow and body. The body grows out of the bar's clock island (panel.morph picks the
// style) and plays the motion backwards, quicker, on close.
//
// Tab contexts for `panel-toggle control-center <ctx>`: home, sound|audio, network, bluetooth, system,
// inbox|notifications, quick. Anything else opens on panel.defaultTab.

#include "core/timer_manager.h"
#include "shell/control_center/control_center_services.h"
#include "shell/panel/panel.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

class Box;
class InputArea;
class Image;
class Label;

namespace kusanagi {

  class CpPage;
  namespace cp {
    class IconButton;
    class Segmented;
  } // namespace cp

  class ControlPanel : public Panel {
  public:
    // These numbers are stored in panel.defaultTab, so they must not change.
    enum Tab : int { Home = 0, System = 1, Inbox = 2, Quick = 3, Sound = 4, Network = 5, Bluetooth = 6 };

    explicit ControlPanel(const ControlCenterServices& services);
    ~ControlPanel() override;

    void create() override;
    void onOpen(std::string_view context) override;
    void onClose() override;
    [[nodiscard]] bool isContextActive(std::string_view context) const override;
    [[nodiscard]] bool handleGlobalKey(std::uint32_t sym, std::uint32_t modifiers, bool pressed, bool preedit) override;
    [[nodiscard]] bool dismissTransientUi() override;

    [[nodiscard]] float preferredWidth() const override { return 560.0F; }
    [[nodiscard]] float preferredHeight() const override { return 600.0F; }
    [[nodiscard]] bool hasDecoration() const override { return false; }
    [[nodiscard]] LayerShellLayer layer() const override { return LayerShellLayer::Overlay; }
    [[nodiscard]] bool dismissOnOutsideClick() const override { return false; }
    [[nodiscard]] bool coversOutput() const noexcept override { return true; }
    bool beginCloseAnimation(std::function<void()> done) override;

    // For the pages.
    void selectTab(int tab);
    void close();
    // Closes, then runs the command once the panel is out of the way (screenshots, pickers).
    void runClosed(std::vector<std::string> command);
    // Closes, then runs an IPC command of this shell, e.g. "session lock".
    void ipcClosed(std::string command);
    [[nodiscard]] bool bluetoothAvailable() const;
    [[nodiscard]] bool showing() const noexcept { return m_showing; }
    void requestPageLayout();

  protected:
    void doLayout(Renderer& renderer, float width, float height) override;
    void doUpdate(Renderer& renderer) override;

  private:
    [[nodiscard]] static int tabForContext(std::string_view context);
    [[nodiscard]] std::vector<int> tabOrder() const;
    [[nodiscard]] std::unique_ptr<CpPage> makePage(int tab);
    void buildHeader();
    void buildTabs();
    void syncHeader(Renderer& renderer);
    void layoutHeader(Renderer& renderer, float width);
    void switchTab(int tab);
    void computeOrigin(float screenW, float screenH);
    void applyMorph();
    void animateOpen(float to);
    void animatePageHeight(float to);
    void startTicker();

    ControlCenterServices m_services;

    // Scene, alive while the panel is open
    Node* m_rootNode = nullptr;
    Box* m_backdrop = nullptr;
    Box* m_shadow = nullptr;
    Box* m_frame = nullptr;
    Node* m_body = nullptr;
    Node* m_header = nullptr;
    Label* m_time = nullptr;
    Label* m_date = nullptr;
    Box* m_avatarBg = nullptr;
    Image* m_avatar = nullptr;
    std::vector<cp::IconButton*> m_headerButtons;
    cp::Segmented* m_tabs = nullptr;
    Node* m_pageClip = nullptr;
    CpPage* m_page = nullptr;

    bool m_showing = false;
    int m_tab = Home;
    int m_lastTab = Home;
    bool m_pageDirty = true;
    bool m_avatarLoaded = false;
    std::string m_timeText;
    std::string m_dateText;
    int m_inboxCount = -1;

    // Open/close morph
    float m_open = 0.0F;
    float m_screenW = 0.0F;
    float m_screenH = 0.0F;
    float m_headerH = 0.0F;
    float m_pageH = 0.0F;      // shown page height, animated toward the page's own
    float m_pageTargetH = 0.0F;
    std::string m_morph = "island";
    std::string m_edge = "top";
    bool m_hasOrigin = false;
    float m_originX = 0.0F;
    float m_originY = 0.0F;
    float m_originW = 0.0F;
    float m_originH = 0.0F;
    float m_targetX = 0.0F;
    float m_barThickness = 28.0F;

    Timer m_ticker;
    Timer m_runLater;
    std::vector<std::string> m_pendingCommand;
    std::string m_pendingIpc;
  };

} // namespace kusanagi
