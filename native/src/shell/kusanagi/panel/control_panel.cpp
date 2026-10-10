#include "shell/kusanagi/panel/control_panel.h"

#include "core/process/process.h"
#include "dbus/bluetooth/bluetooth_service.h"
#include "ipc/ipc_service.h"
#include "notification/notification_manager.h"
#include "render/animation/animation_manager.h"
#include "render/core/renderer.h"
#include "shell/kusanagi/clock_island.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/panel/cp_controls.h"
#include "shell/kusanagi/panel/cp_home.h"
#include "shell/kusanagi/panel/cp_inbox.h"
#include "shell/kusanagi/panel/cp_page.h"
#include "shell/kusanagi/panel/cp_pages.h"
#include "shell/panel/panel_manager.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"
#include "wayland/surface.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace kusanagi {

  namespace {
    float clamp01(float v) { return std::clamp(v, 0.0F, 1.0F); }
    float lerp(float a, float b, float t) { return a + (b - a) * t; }

    // 24-hour time unless the bar clock format asks for AM/PM.
    std::string headerTime(const std::tm& tm) {
      const std::string clock = opt<std::string>("bar", "clock", "HH:mm");
      char buf[32];
      if (clock.find("AP") != std::string::npos || clock.find("ap") != std::string::npos) {
        const int h = tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12;
        std::snprintf(buf, sizeof(buf), "%d:%02d %s", h, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
      } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
      }
      return buf;
    }

    std::string headerDate(const std::tm& tm) {
      static const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
      static const char* months[] = {"January", "February", "March",     "April",   "May",      "June",
                                     "July",    "August",   "September", "October", "November", "December"};
      return std::string(days[tm.tm_wday]) + ", " + std::to_string(tm.tm_mday) + " " + months[tm.tm_mon];
    }

    bool hasClock(const nlohmann::json& modules) {
      if (!modules.is_array()) return false;
      for (const auto& m : modules) {
        if (m.is_object() && m.value("type", std::string()) == "clock") return true;
        if (m.is_object() && hasClock(m.value("modules", nlohmann::json::array()))) return true;
      }
      return false;
    }

    // Used when the bar hasn't published its clock island: a best guess from the bar spec that holds the
    // clock and the bar surfaces on this output. A bar surface narrower than the output (length "auto") is
    // the island itself.
    std::optional<IslandRect> estimateIsland(float screenW, float screenH, const std::vector<InputRect>& bars) {
      const auto& s = settings();
      std::string edge = "top";
      std::string section = "center";
      float size = 28.0F;
      float margin = 4.0F;
      bool found = false;
      if (const auto it = s.find("bars"); it != s.end() && it->is_array() && !it->empty()) {
        for (const auto& b : *it) {
          if (!b.is_object()) continue;
          for (const char* sec : {"start", "center", "end"}) {
            if (!hasClock(b.value(sec, nlohmann::json::array()))) continue;
            edge = b.value("position", std::string("top"));
            section = sec;
            size = b.value("size", 28.0F);
            const auto m = b.value("margin", nlohmann::json::array());
            margin = m.is_array() && !m.empty() && m[0].is_number() ? m[0].get<float>() : 0.0F;
            found = true;
            break;
          }
          if (found) break;
        }
      } else {
        edge = opt<std::string>("bar", "position", "top");
        size = static_cast<float>(opt<double>("bar", "height", 28.0)) - 6.0F;
        section = opt<std::string>("bar", "layout", "centered") == "centered" ? "center" : "start";
        found = true;
      }
      if (!found || edge == "left" || edge == "right") return std::nullopt;
      const bool top = edge == "top";
      for (const auto& r : bars) {
        const bool onEdge = top ? r.y <= 1 : r.y + r.height >= static_cast<int>(screenH) - 1;
        if (r.width > r.height && onEdge && static_cast<float>(r.width) < screenW * 0.9F) {
          return IslandRect{.x = static_cast<float>(r.x), .y = top ? margin : screenH - margin - size,
                            .w = static_cast<float>(r.width), .h = size, .edge = edge};
        }
      }
      constexpr float kGuessW = 230.0F;
      const float x = section == "start" ? 8.0F : section == "end" ? screenW - 8.0F - kGuessW : (screenW - kGuessW) / 2.0F;
      return IslandRect{.x = std::round(x), .y = top ? margin : screenH - margin - size, .w = kGuessW, .h = size, .edge = edge};
    }

    // The first bar's size plus its edge and inner margins.
    std::optional<float> barThicknessFromSettings() {
      const auto& s = settings();
      const auto it = s.find("bars");
      if (it == s.end() || !it->is_array() || it->empty() || !(*it)[0].is_object()) return std::nullopt;
      const auto& b = (*it)[0];
      const float size = b.value("size", 28.0F);
      const auto& m = b.contains("margin") ? b["margin"] : nlohmann::json();
      float edge = 0.0F;
      float inner = 0.0F;
      if (m.is_number()) {
        edge = inner = m.get<float>();
      } else if (m.is_array() && !m.empty() && m[0].is_number()) {
        edge = m[0].get<float>();
        inner = m.size() >= 3 && m[1].is_number() ? m[1].get<float>() : edge;
      } else if (m.is_object()) {
        edge = m.value("edge", 0.0F);
        inner = m.value("inner", 0.0F);
      }
      return size + edge + inner;
    }

    std::tm localNow() {
      const std::time_t now = std::time(nullptr);
      std::tm tm{};
      localtime_r(&now, &tm);
      return tm;
    }
  } // namespace

  ControlPanel::ControlPanel(const ControlCenterServices& services) : m_services(services) {}

  ControlPanel::~ControlPanel() = default;

  int ControlPanel::tabForContext(std::string_view c) {
    if (c == "home" || c == "quick-settings") return Home;
    if (c == "sound" || c == "audio") return Sound;
    if (c == "network" || c == "net" || c == "wifi") return Network;
    if (c == "bluetooth" || c == "bt") return Bluetooth;
    if (c == "system" || c == "monitor") return System;
    if (c == "inbox" || c == "notifications" || c == "notifs") return Inbox;
    if (c == "quick") return Quick;
    return -1;
  }

  bool ControlPanel::bluetoothAvailable() const {
    if (dev::fakeBluetooth()) return true;
    return m_services.bluetooth != nullptr && m_services.bluetooth->state().adapterPresent;
  }

  std::vector<int> ControlPanel::tabOrder() const {
    // Display order. It differs from the enum values, which panel.defaultTab stores.
    if (bluetoothAvailable()) return {Home, Sound, Network, Bluetooth, System, Inbox, Quick};
    return {Home, Sound, Network, System, Inbox, Quick};
  }

  bool ControlPanel::isContextActive(std::string_view context) const {
    const int tab = tabForContext(context);
    return m_showing && tab >= 0 && (tab == Bluetooth && !bluetoothAvailable() ? Network : tab) == m_tab;
  }

  std::unique_ptr<CpPage> ControlPanel::makePage(int tab) {
    switch (tab) {
    case Inbox: return std::make_unique<CpInbox>(*this, m_services);
    case Sound: return std::make_unique<CpSound>(*this, m_services);
    case Network: return std::make_unique<CpNetwork>(*this, m_services);
    case Bluetooth: return std::make_unique<CpBluetooth>(*this, m_services);
    case System: return std::make_unique<CpSystem>(*this, m_services);
    case Quick: return std::make_unique<CpQuick>(*this, m_services);
    default: return std::make_unique<CpHome>(*this, m_services);
    }
  }

  void ControlPanel::create() {
    auto root = ui::node({});
    m_rootNode = root.get();

    // Clicking the dimmed backdrop closes the panel.
    auto backdrop = ui::inputArea({.onClick = [this](const InputArea::PointerData&) { close(); }});
    m_backdrop = static_cast<Box*>(backdrop->addChild(ui::box({})));
    m_backdrop->setFill(Color{0.0F, 0.0F, 0.0F, 0.0F});
    root->addChild(std::move(backdrop));

    m_shadow = static_cast<Box*>(root->addChild(ui::box({})));
    m_shadow->setVisible(kusanagi::shadows());

    // Clicks inside the panel don't reach the backdrop.
    auto frame = ui::inputArea({});
    frame->setClipChildren(true);
    m_frame = static_cast<Box*>(frame->addChild(ui::box({})));
    m_body = frame->addChild(ui::node({}));
    root->addChild(std::move(frame));

    buildHeader();
    buildTabs();
    m_pageClip = m_body->addChild(ui::node({}));

    m_page = nullptr;
    m_pageDirty = true;
    m_avatarLoaded = false;
    m_timeText.clear();
    m_dateText.clear();
    m_inboxCount = -1;
    m_open = 0.0F;
    m_screenW = 0.0F;
    m_pageH = 0.0F;
    m_pageTargetH = 0.0F;
    setRoot(std::move(root));
  }

  void ControlPanel::onOpen(std::string_view context) {
    // Without an adapter, an explicit Bluetooth tab falls back to Network and a default one to Home.
    int tab = tabForContext(context);
    if (tab == Bluetooth && !bluetoothAvailable()) tab = Network;
    if (tab < 0) {
      tab = opt<int>("panel", "defaultTab", 0);
      if (tab < 0 || tab > Bluetooth || (tab == Bluetooth && !bluetoothAvailable())) tab = Home;
    }

    if (m_showing && m_rootNode != nullptr && m_screenW > 0.0F) {
      // Already open and toggled with another tab: just switch.
      switchTab(tab);
      return;
    }
    m_showing = true;
    setPanelShowing(true); // the bar hides the clock island while the panel shows
    m_tab = tab;
    m_lastTab = tab;
    m_pageDirty = true;
    startTicker();
    animateOpen(1.0F);
  }

  void ControlPanel::onClose() {
    // The surface and the scene are going away.
    m_showing = false;
    setPanelShowing(false);
    m_ticker.stop();
    m_rootNode = nullptr;
    m_backdrop = nullptr;
    m_shadow = nullptr;
    m_frame = nullptr;
    m_body = nullptr;
    m_header = nullptr;
    m_time = nullptr;
    m_date = nullptr;
    m_avatarBg = nullptr;
    m_avatar = nullptr;
    m_headerButtons.clear();
    m_tabs = nullptr;
    m_pageClip = nullptr;
    m_page = nullptr;
    m_screenW = 0.0F;
  }

  bool ControlPanel::beginCloseAnimation(std::function<void()> done) {
    if (m_rootNode == nullptr || m_backdrop == nullptr) return false;
    m_showing = false;
    setPanelShowing(false);
    m_ticker.stop();
    AnimationManager* anims = m_animations;
    if (anims == nullptr) return false;
    anims->cancelForOwner(m_backdrop);
    const float from = m_open;
    anims->animate(
        0.0F, 1.0F, 300.0F, Easing::Linear,
        [this, from](float t) {
          m_open = from * (1.0F - cp::bezier(0.3F, 0.0F, 0.8F, 0.15F, t));  // emphasized accelerate
          applyMorph();
        },
        [done = std::move(done)]() { done(); }, m_backdrop
    );
    return true;
  }

  void ControlPanel::animateOpen(float to) {
    if (m_animations == nullptr || m_backdrop == nullptr) {
      m_open = to;
      return;
    }
    m_animations->cancelForOwner(m_backdrop);
    const float from = m_open;
    m_animations->animate(
        0.0F, 1.0F, 560.0F, Easing::Linear,
        [this, from, to](float t) {
          m_open = from + (to - from) * cp::bezier(0.05F, 0.7F, 0.1F, 1.0F, t);  // emphasized decelerate
          applyMorph();
        },
        [this, to]() {
          m_open = to;
          applyMorph();
        },
        m_backdrop
    );
  }

  void ControlPanel::close() { PanelManager::instance().closePanel(); }

  void ControlPanel::runClosed(std::vector<std::string> command) {
    close();
    m_pendingCommand = std::move(command);
    m_runLater.start(std::chrono::milliseconds(kusanagi::ms(300)), [this]() {
      if (!m_pendingCommand.empty()) (void)process::runAsync(m_pendingCommand);
      m_pendingCommand.clear();
    });
  }

  void ControlPanel::ipcClosed(std::string command) {
    close();
    m_pendingIpc = std::move(command);
    m_runLater.start(std::chrono::milliseconds(kusanagi::ms(300)), [this]() {
      if (m_services.ipc != nullptr && !m_pendingIpc.empty()) (void)m_services.ipc->execute(m_pendingIpc);
      m_pendingIpc.clear();
    });
  }

  void ControlPanel::startTicker() {
    // The header clock, media position and mini stats change on their own, so refresh once a second while
    // the panel is open.
    m_ticker.startRepeating(std::chrono::milliseconds(1000), [this]() {
      if (!m_showing) return;
      if (m_page != nullptr) m_page->tick();
      PanelManager::instance().refresh();
    });
  }

  bool ControlPanel::dismissTransientUi() {
    // Escape closes a page's open prompt (the Wi-Fi password) first, and the panel on the next press.
    return m_page != nullptr && m_page->dismissTransient();
  }

  bool ControlPanel::handleGlobalKey(std::uint32_t sym, std::uint32_t modifiers, bool pressed, bool /*preedit*/) {
    if (!pressed || sym != XKB_KEY_Tab || modifiers != 0) return false;
    // Tab doesn't switch tabs while a text field has focus.
    if (const InputArea* f = PanelManager::instance().inputDispatcher().focusedArea();
        f != nullptr && f->textInputClient() != nullptr) {
      return true;
    }
    const auto order = tabOrder();
    const auto it = std::ranges::find(order, m_tab);
    const std::size_t i = it == order.end() ? 0 : static_cast<std::size_t>(it - order.begin());
    switchTab(order[(i + 1) % order.size()]);
    return true;
  }

  void ControlPanel::selectTab(int tab) { switchTab(tab); }

  void ControlPanel::switchTab(int tab) {
    if (tab == m_tab && m_page != nullptr) return;
    m_lastTab = m_tab;
    m_tab = tab;
    m_pageDirty = true;
    if (m_tabs != nullptr) m_tabs->setCurrent(tab, true);
    PanelManager::instance().requestUpdateOnly();
    PanelManager::instance().requestLayout();
  }

  void ControlPanel::requestPageLayout() { PanelManager::instance().requestLayout(); }

  void ControlPanel::buildHeader() {
    m_headerButtons.clear();
    m_header = m_body->addChild(ui::node({}));
    m_time = static_cast<Label*>(m_header->addChild(cp::text("", 28.0F, true)));
    m_date = static_cast<Label*>(m_header->addChild(cp::text("", 11.0F, false, cp::dim())));

    m_avatarBg = static_cast<Box*>(m_header->addChild(ui::box({})));
    m_avatarBg->setFill(cp::textA(0.08F));
    m_avatarBg->setRadius(17.0F);
    m_avatar = static_cast<Image*>(m_header->addChild(ui::image({.fit = ImageFit::Cover, .radius = 17.0F})));

    struct Action {
      char32_t icon;
      std::function<void()> run;
    };
    const Action actions[] = {
        {0xf0493,
         [this]() {
           close();
           if (m_services.ipc != nullptr) (void)m_services.ipc->execute("settings-open");
         }},
        {0xf033e, [this]() { ipcClosed("session lock"); }},
        {0xf0425, [this]() { ipcClosed("panel-toggle session"); }},
    };
    for (const auto& a : actions) {
      auto button = std::make_unique<cp::IconButton>(a.icon);
      button->setOnActivate(a.run);
      m_headerButtons.push_back(static_cast<cp::IconButton*>(m_header->addChild(std::move(button))));
    }
  }

  void ControlPanel::buildTabs() {
    auto tabs = std::make_unique<cp::Segmented>(10.0F);
    tabs->setOnPicked([this](int v) { switchTab(v); });
    m_tabs = static_cast<cp::Segmented*>(m_body->addChild(std::move(tabs)));
  }

  void ControlPanel::syncHeader(Renderer& /*renderer*/) {
    const std::tm tm = localNow();
    const std::string t = headerTime(tm);
    const std::string d = headerDate(tm);
    bool changed = false;
    if (t != m_timeText) {
      m_timeText = t;
      m_time->setText(t);
      changed = true;
    }
    if (d != m_dateText) {
      m_dateText = d;
      m_date->setText(d);
      changed = true;
    }
    const int count =
        m_services.notifications != nullptr ? static_cast<int>(m_services.notifications->history().size()) : 0;
    if (count != m_inboxCount || changed) {
      m_inboxCount = count;
      std::vector<cp::Segment> options{{"Home", Home, 0xf02dc}, {"Sound", Sound, 0xf057e}, {"Net", Network, 0xf06f3}};
      if (bluetoothAvailable()) options.push_back({"BT", Bluetooth, 0xf00af});
      options.push_back({"System", System, 0xf012a});
      options.push_back({count > 0 ? "Inbox " + std::to_string(count) : "Inbox", Inbox, 0xf009a});
      options.push_back({"Quick", Quick, 0xf0493});
      m_tabs->setOptions(std::move(options), m_tab);
      changed = true;
    }
    if (changed) PanelManager::instance().requestLayout();
  }

  void ControlPanel::layoutHeader(Renderer& renderer, float width) {
    const std::string style = opt<std::string>("panel", "header", "big");
    const bool hidden = style == "hidden";
    const bool compact = style == "compact";
    m_header->setVisible(!hidden);
    m_headerH = hidden ? 0.0F : compact ? 34.0F : 48.0F;
    if (hidden) return;

    m_time->setFontSize(compact ? 18.0F : 28.0F);
    m_time->measure(renderer);
    m_date->measure(renderer);
    const float colH = m_time->height() + m_date->height();
    const float colY = std::round((m_headerH - colH) / 2.0F);
    m_time->setPosition(0.0F, colY);
    m_date->setPosition(0.0F, colY + m_time->height());

    // Buttons from the right edge, then the avatar.
    float x = width;
    for (auto it = m_headerButtons.rbegin(); it != m_headerButtons.rend(); ++it) {
      x -= 34.0F;
      (*it)->setSize(34.0F, 34.0F);
      (*it)->setPosition(x, std::round((m_headerH - 34.0F) / 2.0F));
      (*it)->layout(renderer);
      x -= 4.0F;
    }
    x -= 6.0F + 4.0F + 34.0F;
    const float ay = std::round((m_headerH - 34.0F) / 2.0F);
    m_avatarBg->setPosition(x, ay);
    m_avatarBg->setSize(34.0F, 34.0F);
    m_avatar->setPosition(x, ay);
    m_avatar->setSize(34.0F, 34.0F);
    if (!m_avatarLoaded) {
      m_avatarLoaded = true;
      if (const char* home = std::getenv("HOME"); home != nullptr) {
        const std::string face = std::string(home) + "/.face";
        std::error_code ec;
        if (std::filesystem::exists(face, ec)) (void)m_avatar->setSourceFile(renderer, face, 68, false, true);
      }
    }
  }

  void ControlPanel::doUpdate(Renderer& renderer) {
    if (m_rootNode == nullptr) return;
    syncHeader(renderer);
    if (m_page != nullptr && !m_pageDirty && m_page->sync(renderer)) PanelManager::instance().requestLayout();
  }

  void ControlPanel::computeOrigin(float screenW, float screenH) {
    wl_output* output = PanelManager::instance().attachedPanelOutput();
    std::optional<IslandRect> island = clockIsland(output);
    const auto bars = PanelManager::instance().barRectsForOutput(output);
    for (const auto& r : bars) m_barThickness = static_cast<float>(std::min(r.width, r.height));
    if (const auto t = barThicknessFromSettings()) m_barThickness = *t;
    if (!island) island = estimateIsland(screenW, screenH, bars);

    m_hasOrigin = island.has_value();
    m_edge = island ? island->edge : opt<std::string>("bar", "position", "top");
    if (island) {
      m_originX = island->x;
      m_originY = island->y;
      m_originW = island->w;
      m_originH = island->h;
    }
    const bool side = m_edge == "left" || m_edge == "right";
    m_morph = opt<std::string>("panel", "morph", "island");
    if (m_morph == "island" && (!m_hasOrigin || side)) m_morph = "fade";

    // Final spot: under the clock. Centred, or aligned with the clock when it sits in an outer third.
    const float targetW = static_cast<float>(opt<double>("panel", "width", 560.0));
    if (m_morph == "sheet") {
      m_targetX = screenW - targetW - (m_edge == "right" ? m_barThickness + 8.0F : 8.0F);
      return;
    }
    const float lo = m_edge == "left" ? m_barThickness + 6.0F : 6.0F;
    const float hi = screenW - (m_edge == "right" ? m_barThickness + 6.0F : 6.0F);
    if (!m_hasOrigin || side) {
      m_targetX = std::round((lo + hi - targetW) / 2.0F);
      return;
    }
    const float c = m_originX + m_originW / 2.0F;
    m_targetX = c < screenW / 3.0F         ? std::max(lo, m_originX)
                : c > screenW * 2.0F / 3.0F ? std::min(hi - targetW, m_originX + m_originW - targetW)
                                             : std::round((screenW - targetW) / 2.0F);
  }

  void ControlPanel::doLayout(Renderer& renderer, float width, float height) {
    if (m_rootNode == nullptr) return;
    const bool first = m_screenW <= 0.0F;
    m_screenW = width;
    m_screenH = height;
    m_rootNode->setSize(width, height);
    if (first) {
      computeOrigin(width, height);
      syncHeader(renderer);
    }

    const float bodyW = static_cast<float>(opt<double>("panel", "width", 560.0)) - 40.0F;
    m_body->setSize(bodyW, height);
    layoutHeader(renderer, bodyW);

    float y = m_headerH > 0.0F ? m_headerH + 16.0F : 0.0F;
    const bool tabs = opt<bool>("panel", "tabs", true);
    m_tabs->setVisible(tabs);
    if (tabs) {
      m_tabs->setPosition(0.0F, y);
      m_tabs->setSize(bodyW, 34.0F);
      m_tabs->layout(renderer);
      y += 34.0F + 16.0F;
    }
    m_pageClip->setPosition(0.0F, y);

    // The page is rebuilt when the tab changed.
    bool fresh = false;
    if (m_pageDirty || m_page == nullptr) {
      m_pageDirty = false;
      fresh = true;
      if (m_page != nullptr) (void)m_pageClip->removeChild(m_page);
      auto page = makePage(m_tab);
      m_page = static_cast<CpPage*>(m_pageClip->addChild(std::move(page)));
      (void)m_page->sync(renderer);
    }
    const float pageH = std::round(m_page->layout(renderer, bodyW));
    m_page->setSize(bodyW, pageH);

    if (fresh && !first && m_animations != nullptr) {
      // The new page fades in and slides in from the side its tab is on.
      const auto order = tabOrder();
      const auto pos = [&order](int t) { return std::ranges::find(order, t) - order.begin(); };
      const float from = m_tab == m_lastTab ? 0.0F : pos(m_tab) >= pos(m_lastTab) ? 28.0F : -28.0F;
      m_page->setPosition(from, 0.0F);
      m_page->setOpacity(0.0F);
      CpPage* page = m_page;
      m_animations->animate(
          0.0F, 1.0F, 380.0F, Easing::Linear,
          [page, from](float t) { page->setPosition(std::round(from * (1.0F - cp::outQuint(t))), 0.0F); }, {}, page
      );
      m_animations->animate(0.0F, 1.0F, 260.0F, Easing::EaseOutCubic, [page](float t) { page->setOpacity(t); }, {},
                            m_pageClip);
    }

    if (pageH != m_pageTargetH) {
      m_pageTargetH = pageH;
      if (!first && m_open >= 1.0F && m_animations != nullptr) {
        animatePageHeight(pageH);
      } else {
        m_pageH = pageH;
      }
    }
    applyMorph();
  }

  void ControlPanel::animatePageHeight(float to) {
    m_animations->cancelForOwner(m_frame);
    const float from = m_pageH;
    m_animations->animate(
        0.0F, 1.0F, 380.0F, Easing::Linear,
        [this, from, to](float t) {
          m_pageH = from + (to - from) * cp::outQuint(t);
          applyMorph();
        },
        {}, m_frame
    );
  }

  // Panel geometry for the current open progress (0 = the clock island, 1 = the full panel).
  void ControlPanel::applyMorph() {
    if (m_rootNode == nullptr || m_screenW <= 0.0F) return;
    const float W = m_screenW;
    const float H = m_screenH;
    const float open = m_open;
    const float wP = clamp01(open * 1.5F);               // widen first,
    const float hP = clamp01((open - 0.08F) / 0.92F);    // then grow down,
    const float contentP = clamp01((open - 0.42F) / 0.58F); // and show the content last

    const bool sheet = m_morph == "sheet";
    const bool island = m_morph == "island";
    const bool fade = m_morph == "fade";
    const bool fromBottom = m_edge == "bottom";
    const float targetW = static_cast<float>(opt<double>("panel", "width", 560.0));
    const float bodyH = (m_headerH > 0.0F ? m_headerH + 16.0F : 0.0F)
        + (opt<bool>("panel", "tabs", true) ? 34.0F + 16.0F : 0.0F) + m_pageH;
    const float fullH = bodyH + 40.0F;
    const float sheetTop = m_edge == "top" ? m_barThickness + 8.0F : 8.0F;
    const float sheetH = H - sheetTop - (fromBottom ? m_barThickness + 8.0F : 8.0F);

    const float originX = sheet ? W + 12.0F : !island ? m_targetX : m_originX;
    const float originW = !island ? targetW : m_originW;
    const float originH = sheet ? sheetH : m_morph == "drop" ? 0.0F : fade ? fullH : m_originH;
    const bool side = m_edge == "left" || m_edge == "right";
    const float edge = !m_hasOrigin || side ? 4.0F : fromBottom ? H - m_originY - m_originH : m_originY;

    const float w = std::round(lerp(originW, targetW, wP));
    const float h = sheet ? sheetH : std::round(lerp(originH, fullH, hP));
    const float x = std::round(lerp(originX, m_targetX, sheet ? open : wP));
    const float y = sheet ? sheetTop : fromBottom ? H - edge - h : edge;

    const float barRadius = static_cast<float>(opt<double>("bar", "radius", 10.0));
    const float radius = lerp(barRadius, kusanagi::radius(), wP);
    const float barOpacity = static_cast<float>(opt<double>("bar", "opacity", 0.5));
    const float panelOpacity = static_cast<float>(opt<double>("panel", "opacity", 0.95));

    m_backdrop->setSize(W, H);
    if (auto* area = m_backdrop->parent(); area != nullptr) area->setSize(W, H);
    m_backdrop->setFill(Color{0.0F, 0.0F, 0.0F,
                              static_cast<float>(opt<double>("look", "backdrop", 0.25)) * 0.75F * open});

    Node* frame = m_frame->parent();
    frame->setPosition(x, y);
    frame->setSize(w, h);
    frame->setOpacity(fade ? std::min(1.0F, open * 1.4F) : sheet ? std::min(1.0F, open * 2.0F) : 1.0F);
    frame->setTransformOrigin(w / 2.0F, fromBottom ? h : 0.0F);
    frame->setScale(fade ? 0.95F + 0.05F * open : 1.0F);
    m_frame->setSize(w, h);
    m_frame->setRadius(radius);
    m_frame->setFill(cp::bgPanel(lerp(std::max(barOpacity, 0.5F), panelOpacity, wP)));
    const ColorSpec border = kusanagi::surfaceBorder();
    m_frame->setBorder(scaleAlpha(border, open), kusanagi::surfaceBorderWidth());

    if (m_shadow->visible()) {
      RoundedRectStyle style{
          .fill = Color{0.0F, 0.0F, 0.0F, 0.45F * open},
          .radius = Radii{radius, radius, radius, radius},
          .softness = 30.0F,
          .outerShadow = true,
          .shadowCutoutOffsetX = 0.0F,
          .shadowCutoutOffsetY = 10.0F,
      };
      m_shadow->setStyle(style);
      m_shadow->setPosition(x, y + 10.0F);
      m_shadow->setSize(w, h);
      m_shadow->setOpacity(frame->opacity());
    }

    m_body->setPosition(20.0F, std::round(20.0F - 10.0F * (1.0F - contentP)));
    m_body->setOpacity(contentP);
    m_pageClip->setSize(m_body->width(), m_pageH);
  }

} // namespace kusanagi
