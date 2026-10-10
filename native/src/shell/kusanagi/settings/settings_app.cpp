#include "shell/kusanagi/settings/settings_app.h"

#include "core/deferred_call.h"
#include "core/files/resource_paths.h"
#include "core/input/key_modifiers.h"
#include "core/input/keybind_matcher.h"
#include "core/log.h"
#include "core/ui_phase.h"
#include "cursor-shape-v1-client-protocol.h"
#include "ipc/ipc_service.h"
#include "render/core/render_styles.h"
#include "render/core/renderer.h"
#include "render/render_context.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"
#include "wayland/toplevel_surface.h"
#include "wayland/wayland_connection.h"
#include "wayland/wayland_seat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <linux/input-event-codes.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace kusanagi {

  namespace {
    constexpr Logger kLog("kusanagi-settings");
    constexpr auto kPointer = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;

    constexpr float kWidth = 1080.0F;
    constexpr float kHeight = 720.0F;
    constexpr float kMinWidth = 820.0F;
    constexpr float kMinHeight = 520.0F;
    constexpr float kSidebarW = 250.0F;
    constexpr float kNavTop = 84.0F + 34.0F + 12.0F; // Below the search field.
    constexpr float kItemH = 38.0F;
    constexpr float kGroupH = 26.0F;
    // Keep the old app id so existing window rules that float it (appid:^(org.quickshell)$) still apply.
    constexpr const char* kAppId = "org.quickshell";

    std::string lower(std::string s) {
      for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return s;
    }

    std::string trim(const std::string& s) {
      std::size_t a = 0, b = s.size();
      while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
      while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
      return s.substr(a, b - a);
    }

    void verticalGradient(Box& box, const Color& top, const Color& bottom, float radius) {
      RoundedRectStyle st;
      st.fillMode = FillMode::LinearGradient;
      st.gradientDirection = GradientDirection::Vertical;
      st.gradientStops = {GradientStop{0.0F, top}, GradientStop{1.0F, bottom}, GradientStop{1.0F, bottom},
                          GradientStop{1.0F, bottom}};
      st.fill = top;
      st.radius = Radii(radius, radius, radius, radius);
      box.setStyle(st);
    }

    Color withAlpha(const ColorSpec& c, float a) {
      Color out = sp::resolved(c);
      out.a *= a;
      return out;
    }
  } // namespace

  struct SettingsApp::NavItem : public Node {
    NavItem(SettingsApp& owner, const sp::PageInfo& page) : app(owner), info(page) {
      group = static_cast<Label*>(addChild(sp::makeText("", 10.0F, true, sp::textA(0.4F))));
      if (info.group[0] != '\0') sp::setSpacedText(*group, info.group, 1.5F);
      area = static_cast<InputArea*>(addChild(ui::inputArea({.cursorShape = kPointer})));
      tile = static_cast<Box*>(area->addChild(ui::box({})));
      tile->setSize(24.0F, 24.0F);
      tile->setRadius(7.0F);
      icon = static_cast<Label*>(area->addChild(sp::makeIcon(info.icon, 14.0F)));
      name = static_cast<Label*>(area->addChild(sp::makeText(info.name, 12.0F)));
      name->setMaxLines(1);
      area->setOnEnter([this](const InputArea::PointerData&) { restyle(true); });
      area->setOnLeave([this]() { restyle(true); });
      area->setOnClick([this](const InputArea::PointerData&) {
        const std::string id = info.id;
        DeferredCall::callLater([a = &app, id]() { a->selectPage(id); });
      });
      restyle(false);
    }

    void restyle(bool animate) {
      const bool hov = area->hovered();
      const ColorSpec t = active ? sp::accent() : sp::textA(hov ? 0.1F : 0.06F);
      const ColorSpec i = active ? sp::bgPanel() : hov ? sp::textA(1.0F) : sp::dim();
      if (animate) {
        sp::tweenColor(*tile, tileColor, t, 180, [this](const ColorSpec& c) { tile->setFill(c); });
        sp::tweenColor(*icon, iconColor, i, 160, [this](const ColorSpec& c) { icon->setColor(c); });
      } else {
        tile->setFill(t);
        icon->setColor(i);
      }
      tileColor = t;
      iconColor = i;
      name->setFontWeight(active ? FontWeight::Bold : FontWeight::Normal);
      name->setColor(active || hov ? sp::textA(1.0F) : sp::textA(0.75F));
    }

    [[nodiscard]] float itemH() const { return kItemH + (hasGroup ? kGroupH : 0.0F); }

    void place(Renderer& renderer, float w, bool first) {
      const float top = hasGroup ? kGroupH : 0.0F;
      group->setVisible(hasGroup);
      if (hasGroup) {
        group->measure(renderer);
        group->setPosition(12.0F, first ? 4.0F : 8.0F);
      }
      area->setPosition(0.0F, top);
      area->setSize(w, kItemH);
      name->measure(renderer);
      const float rowH = std::max(24.0F, name->height());
      tile->setPosition(14.0F, 7.0F + std::round((rowH - 24.0F) / 2.0F));
      icon->measure(renderer);
      icon->setPosition(14.0F + std::round((24.0F - icon->width()) / 2.0F),
                        tile->y() + std::round((24.0F - icon->height()) / 2.0F));
      // Matches the classic look: the name sits at the top of the row instead of being centred.
      name->setPosition(14.0F + 24.0F + 12.0F, 7.0F);
      setSize(w, itemH());
    }

    SettingsApp& app;
    const sp::PageInfo& info;
    Label* group = nullptr;
    InputArea* area = nullptr;
    Box* tile = nullptr;
    Label* icon = nullptr;
    Label* name = nullptr;
    ColorSpec tileColor;
    ColorSpec iconColor;
    bool active = false;
    bool hasGroup = false;
    bool shown = true;
  };

  SettingsApp& SettingsApp::instance() {
    static SettingsApp app;
    return app;
  }

  void SettingsApp::initialize(WaylandConnection& wayland, RenderContext* renderContext, IpcService* ipc) {
    m_wayland = &wayland;
    m_renderContext = renderContext;
    m_ipc = ipc;
  }

  void SettingsApp::registerIpc(IpcService& ipc) {
    ipc.bind(kusanagi::cli::msg::kusanagiSettings, [this](const std::string& args) -> std::string {
      std::string a = trim(args);
      std::string action = a.substr(0, a.find(' '));
      std::string page = a.find(' ') == std::string::npos ? std::string() : trim(a.substr(a.find(' ') + 1));
      if (action.empty()) action = "toggle";
      const auto known = [](const std::string& id) {
        return id.empty() || std::ranges::any_of(sp::pages(), [&id](const sp::PageInfo& p) { return id == p.id; });
      };
      if (!known(page)) return "error: no settings page '" + page + "'\n";
      if (action == "toggle") {
        toggle(page);
      } else if (action == "open" || action == "show") {
        open(page);
      } else if (action == "page") {
        if (page.empty()) return "error: kusanagi-settings page takes a page id\n";
        open(page);
      } else if (action == "hide") {
        close();
      } else {
        return "error: kusanagi-settings takes toggle [page] | open [page] | page <id> | hide\n";
      }
      return isOpen() ? "open " + m_page + "\n" : "closed\n";
    });
  }

  void SettingsApp::toggle(const std::string& page) {
    if (isOpen() && (page.empty() || page == m_page)) {
      close();
      return;
    }
    open(page);
  }

  void SettingsApp::open(const std::string& page) {
    if (!page.empty() && page != m_page) {
      m_page = page;
      m_pageNeedsBuild = true;
    }
    if (m_page.empty()) m_page = sp::pages().front().id;
    if (isOpen()) {
      if (m_pageNeedsBuild) selectPage(m_page);
      m_wayland->activateSurface(m_surface->wlSurface());
      return;
    }
    if (m_wayland == nullptr || m_renderContext == nullptr || !m_wayland->hasXdgShell()) return;

    wl_output* output = m_wayland->lastPointerOutput();
    if (output == nullptr && !m_wayland->outputs().empty()) output = m_wayland->outputs().front().output;
    m_output = output;

    sp::setHost(sp::Host{
        .changed = [this]() { onSettingsChanged(); },
        .requestLayout = [this]() { requestLayout(); },
        .ipc = [this](const std::string& line) { return m_ipc != nullptr ? m_ipc->execute(line) : std::string("error\n"); },
    });

    m_surface = std::make_unique<ToplevelSurface>(*m_wayland);
    m_surface->setRenderContext(m_renderContext);
    m_surface->setAnimationManager(&m_animations);
    m_surface->setClosedCallback([this]() { DeferredCall::callLater([this]() { close(); }); });
    m_surface->setOutputChangedCallback([this](wl_output* o) {
      m_output = o;
      requestLayout();
    });
    m_surface->setConfigureCallback([this](std::uint32_t, std::uint32_t) { requestLayout(); });
    m_surface->setPrepareFrameCallback([this](bool needsUpdate, bool needsLayout) { prepareFrame(needsUpdate, needsLayout); });
    m_surface->setUpdateCallback([]() {});
    ToplevelSurfaceConfig cfg{
        .width = static_cast<std::uint32_t>(kWidth),
        .height = static_cast<std::uint32_t>(kHeight),
        .minWidth = static_cast<std::uint32_t>(kMinWidth),
        .minHeight = static_cast<std::uint32_t>(kMinHeight),
        .title = "Kusanagi Settings",
        .appId = kAppId,
    };
    if (!m_surface->initialize(output, cfg)) {
      kLog.warn("couldn't create the settings window");
      m_surface.reset();
      return;
    }
    m_sceneDirty = true;
    m_pageNeedsBuild = true;
    m_pointerInside = false;
  }

  void SettingsApp::close() {
    if (!isOpen()) return;
    sp::flush();
    destroyWindow();
  }

  void SettingsApp::destroyWindow() {
    if (m_surface != nullptr) {
      m_dispatcher.pointerLeave();
      m_dispatcher.setSceneRoot(nullptr);
      m_surface->setSceneRoot(nullptr);
    }
    m_animations.cancelAll();
    m_root.reset();
    m_surface.reset();
    m_nav.clear();
    m_bg = m_sidebar = m_divider = m_highlight = m_headTile = m_indicator = nullptr;
    m_logo = nullptr;
    m_brand = m_brandSub = m_headIcon = m_headName = m_headDesc = nullptr;
    m_search = nullptr;
    m_navView = m_scroller = nullptr;
    m_navContent = m_main = m_pageHolder = nullptr;
    m_pageBody = nullptr;
    m_pointerInside = false;
    m_highlightPlaced = false;
    m_query.clear();
    m_scroll = m_scrollTarget = m_navScroll = 0.0F;
    sp::setHost({});
  }

  void SettingsApp::requestLayout() {
    if (m_surface != nullptr) m_surface->requestLayout();
  }

  void SettingsApp::requestRedraw() {
    if (m_surface != nullptr) m_surface->requestRedraw();
  }

  bool SettingsApp::ownsKeyboardSurface(wl_surface* surface) const noexcept {
    return m_surface != nullptr && surface != nullptr && surface == m_surface->wlSurface();
  }

  const sp::PageInfo& SettingsApp::currentPage() const {
    for (const auto& p : sp::pages()) {
      if (m_page == p.id) return p;
    }
    return sp::pages().front();
  }

  std::vector<const sp::PageInfo*> SettingsApp::shownPages() const {
    std::vector<const sp::PageInfo*> out;
    const std::string q = lower(trim(m_query));
    for (const auto& p : sp::pages()) {
      if (q.empty() || lower(std::string(p.name) + " " + p.keys).find(q) != std::string::npos) out.push_back(&p);
    }
    return out;
  }

  void SettingsApp::selectPage(const std::string& id) {
    if (id == m_page && !m_pageNeedsBuild) return;
    m_page = id;
    m_pageNeedsBuild = true;
    for (NavItem* n : m_nav) {
      const bool active = id == n->info.id;
      if (active != n->active) {
        n->active = active;
        n->restyle(true);
      }
    }
    moveHighlight(true);
    requestLayout();
  }

  void SettingsApp::applySearch(const std::string& query) {
    m_query = query;
    m_navScroll = 0.0F;
    m_highlightPlaced = false; // The list reflows, so the highlight jumps instead of sliding.
    requestLayout();
  }

  void SettingsApp::buildPage() {
    m_pageNeedsBuild = false;
    if (m_pageHolder == nullptr) return;
    m_animations.cancelForOwner(m_pageHolder);
    if (m_pageBody != nullptr) {
      m_pageHolder->removeChild(m_pageBody);
      m_pageBody = nullptr;
    }
    const sp::PageInfo& info = currentPage();
    m_page = info.id;
    auto body = std::make_unique<sp::Column>(22.0F);
    if (info.build != nullptr) {
      info.build(*body);
    } else {
      sp::buildPlaceholder(*body, info);
    }
    m_pageBody = static_cast<sp::Column*>(m_pageHolder->addChild(std::move(body)));
    m_headIcon->setText(std::string(sp::utf8Of(info.icon)));
    m_headName->setText(info.name);
    m_headDesc->setText(info.desc);
    for (NavItem* n : m_nav) {
      const bool active = m_page == n->info.id;
      if (active != n->active) {
        n->active = active;
        n->restyle(false);
      }
    }
    if (m_keepScroll) {
      m_keepScroll = false;
      m_pageInY = 0.0F;
      return;
    }
    // New page: back to the top, fade in and slide up.
    m_scroll = m_scrollTarget = 0.0F;
    m_pageBody->setOpacity(0.0F);
    m_pageInY = 14.0F;
    m_animations.animate(
        0.0F, 1.0F, 240.0F, Easing::Linear, [this](float t) {
          if (m_pageBody != nullptr) m_pageBody->setOpacity(sp::outCubic(t));
        },
        {}, m_pageHolder
    );
    m_animations.animate(
        0.0F, 1.0F, 340.0F, Easing::Linear, [this](float t) {
          m_pageInY = 14.0F * (1.0F - sp::outQuint(t));
          if (m_pageHolder != nullptr) m_pageHolder->setPosition(36.0F, std::round(m_pageInY - m_scroll));
        },
        {}, m_pageHolder
    );
  }

  void SettingsApp::scrollPageTo(float offset, bool animate) {
    if (m_scroller == nullptr) return;
    const float maxOff = std::max(0.0F, m_pageH + 40.0F - m_scroller->height());
    const float to = std::clamp(offset, 0.0F, maxOff);
    m_scrollTarget = to;
    m_animations.cancelForOwner(m_scroller);
    if (!animate) {
      m_scroll = to;
      requestLayout();
      return;
    }
    const float from = m_scroll;
    m_animations.animate(
        0.0F, 1.0F, 160.0F, Easing::EaseOutCubic, [this, from, to](float t) {
          m_scroll = from + (to - from) * t;
          requestLayout();
        },
        {}, m_scroller
    );
  }

  void SettingsApp::moveHighlight(bool animate) {
    if (m_highlight == nullptr) return;
    NavItem* cur = nullptr;
    for (NavItem* n : m_nav) {
      if (n->shown && m_page == n->info.id) cur = n;
    }
    m_highlight->setVisible(cur != nullptr);
    if (cur == nullptr) return;
    const float top = cur->y() + (cur->hasGroup ? kGroupH : 0.0F);
    const float h = kItemH;
    if (!animate || !m_highlightPlaced) {
      m_highlightY = top;
      m_highlightH = h;
      m_highlightPlaced = true;
      m_highlight->setPosition(0.0F, m_highlightY);
      m_highlight->setSize(m_navView->width(), m_highlightH);
      return;
    }
    m_animations.cancelForOwner(m_highlight);
    const float fromY = m_highlightY;
    const float fromH = m_highlightH;
    m_animations.animate(
        0.0F, 1.0F, 220.0F, Easing::EaseInOutQuad, [this, fromY, fromH, top, h](float t) {
          m_highlightY = fromY + (top - fromY) * t;
          m_highlightH = fromH + (h - fromH) * t;
          if (m_highlight != nullptr) {
            m_highlight->setPosition(0.0F, std::round(m_highlightY));
            m_highlight->setSize(m_highlight->width(), m_highlightH);
          }
        },
        {}, m_highlight
    );
  }

  void SettingsApp::onSettingsChanged() {
    if (m_pageBody != nullptr) m_pageBody->syncTree();
    requestLayout();
  }

  void SettingsApp::onConfigReload() {
    if (!isOpen()) return;
    sp::reapplyPending();
    if (kusanagi::font() != m_builtFont) {
      // Every label carries its font, so rebuild everything (keeping page, search and scroll).
      m_sceneDirty = true;
      m_pageNeedsBuild = true;
      m_keepScroll = true;
      requestLayout();
      return;
    }
    onSettingsChanged();
    requestRedraw();
  }

  void SettingsApp::buildScene() {
    m_sceneDirty = false;
    m_dispatcher.setSceneRoot(nullptr);
    m_surface->setSceneRoot(nullptr);
    m_animations.cancelAll();
    m_nav.clear();
    m_pageBody = nullptr;
    m_highlightPlaced = false;
    m_builtFont = kusanagi::font();
    const std::string keepQuery = m_query;

    m_root = ui::node({});
    m_root->setAnimationManager(&m_animations);
    m_bg = static_cast<Box*>(m_root->addChild(ui::box({})));

    m_sidebar = static_cast<Box*>(m_root->addChild(ui::box({})));
    m_logo = static_cast<Image*>(m_sidebar->addChild(ui::image({.fit = ImageFit::Contain})));
    m_brand = static_cast<Label*>(m_sidebar->addChild(sp::makeText("", 15.0F, true)));
    sp::setSpacedText(*m_brand, "KUSANAGI", 4.0F);
    m_brandSub = static_cast<Label*>(m_sidebar->addChild(sp::makeText("", 10.0F, false, sp::dim())));
    sp::setSpacedText(*m_brandSub, "settings", 1.0F);
    m_search = static_cast<sp::Field*>(m_sidebar->addChild(std::make_unique<sp::Field>(
        std::nullopt, sp::FieldOpts{
                          .width = kSidebarW - 32.0F,
                          .placeholder = "Search settings",
                          .icon = 0xf0349,
                          .onEdited = [this](const std::string& t) { applySearch(t); },
                          .onAccepted = [this](const std::string&) {
                            const auto shown = shownPages();
                            if (!shown.empty()) {
                              const std::string id = shown.front()->id;
                              DeferredCall::callLater([this, id]() { selectPage(id); });
                            }
                          },
                      }
    )));
    if (!keepQuery.empty()) m_search->setText(keepQuery);
    m_navView = static_cast<InputArea*>(m_sidebar->addChild(ui::inputArea({})));
    m_navView->setClipChildren(true);
    m_navView->setOnAxisHandler([this](const InputArea::PointerData& d) {
      if (d.axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return false;
      const float maxOff = std::max(0.0F, m_navContentH - m_navView->height());
      m_navScroll = std::clamp(m_navScroll + d.scrollDelta(40.0F), 0.0F, maxOff);
      requestLayout();
      return true;
    });
    m_navContent = m_navView->addChild(ui::node({}));
    m_highlight = static_cast<Box*>(m_navContent->addChild(ui::box({})));
    for (const auto& p : sp::pages()) {
      auto item = std::make_unique<NavItem>(*this, p);
      item->active = m_page == p.id;
      item->restyle(false);
      m_nav.push_back(static_cast<NavItem*>(m_navContent->addChild(std::move(item))));
    }
    m_divider = static_cast<Box*>(m_root->addChild(ui::box({})));

    m_main = m_root->addChild(ui::node({}));
    m_headTile = static_cast<Box*>(m_main->addChild(ui::box({})));
    m_headIcon = static_cast<Label*>(m_main->addChild(sp::makeIcon(0, 24.0F, sp::accent())));
    m_headName = static_cast<Label*>(m_main->addChild(sp::makeText("", 24.0F, true)));
    m_headName->setMaxLines(1);
    m_headDesc = static_cast<Label*>(m_main->addChild(sp::makeText("", 12.0F, false, sp::dim())));
    m_headDesc->setMaxLines(1);
    m_scroller = static_cast<InputArea*>(m_main->addChild(ui::inputArea({})));
    m_scroller->setClipChildren(true);
    m_scroller->setOnAxisHandler([this](const InputArea::PointerData& d) {
      if (d.axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return false;
      scrollPageTo(m_scrollTarget + d.scrollDelta(60.0F), true);
      return true;
    });
    m_pageHolder = m_scroller->addChild(ui::node({}));
    m_indicator = static_cast<Box*>(m_main->addChild(ui::box({})));
    m_indicator->setFill(sp::textA(0.2F));
    m_indicator->setRadius(1.5F);

    m_pageNeedsBuild = true;
    m_logoLoaded = false;

    m_dispatcher.setTextInputContext(m_surface->wlSurface(), m_wayland->textInputService());
    m_dispatcher.setCursorShapeCallback([this](std::uint32_t serial, std::uint32_t shape) {
      m_wayland->setCursorShape(serial, shape);
    });
    m_dispatcher.setSceneRoot(m_root.get());
    m_surface->setSceneRoot(m_root.get());
  }

  void SettingsApp::layoutSidebar(Renderer& renderer) {
    const float h = m_root->height();
    m_sidebar->setPosition(0.0F, 0.0F);
    m_sidebar->setSize(kSidebarW, h);
    m_sidebar->setFill(sp::textA(0.03F));

    m_brand->measure(renderer);
    m_brandSub->measure(renderer);
    const float colH = m_brand->height() + 1.0F + m_brandSub->height();
    const float rowH = std::max(38.0F, colH);
    if (!m_logoLoaded) {
      m_logoLoaded = true;
      (void)m_logo->setSourceFile(renderer, paths::assetPath("kusanagi-logo.svg").string(), 76, true);
    }
    m_logo->setSize(38.0F, 38.0F);
    m_logo->setPosition(22.0F, 26.0F + std::round((rowH - 38.0F) / 2.0F));
    const float cy = 26.0F + std::round((rowH - colH) / 2.0F);
    m_brand->setPosition(22.0F + 38.0F + 12.0F, cy);
    m_brandSub->setPosition(22.0F + 38.0F + 12.0F, cy + m_brand->height() + 1.0F);

    m_search->place(renderer, kSidebarW - 32.0F);
    m_search->setPosition(16.0F, 84.0F);

    const float navW = kSidebarW - 20.0F;
    m_navView->setPosition(10.0F, kNavTop);
    m_navView->setSize(navW, std::max(0.0F, h - kNavTop - 14.0F));
    const auto shown = shownPages();
    const bool searching = !trim(m_query).empty();
    float y = 0.0F;
    bool first = true;
    for (NavItem* n : m_nav) {
      n->shown = std::ranges::find(shown, &n->info) != shown.end();
      n->setVisible(n->shown);
      if (!n->shown) continue;
      n->hasGroup = n->info.group[0] != '\0' && !searching;
      if (!first) y += 2.0F;
      n->place(renderer, navW, first);
      n->setPosition(0.0F, y);
      y += n->itemH();
      first = false;
    }
    m_navContentH = y;
    m_navScroll = std::clamp(m_navScroll, 0.0F, std::max(0.0F, m_navContentH - m_navView->height()));
    m_navContent->setPosition(0.0F, -std::round(m_navScroll));
    m_navContent->setSize(navW, y);
    m_highlight->setRadius(12.0F);
    m_highlight->setFill(sp::accent(0.13F));
    m_highlight->setBorder(sp::accent(0.22F), 1.0F);
    if (!m_highlightPlaced) moveHighlight(false);
    m_highlight->setSize(navW, m_highlightH);
  }

  void SettingsApp::layoutScene(Renderer& renderer) {
    const auto w = static_cast<float>(m_surface->width());
    const auto h = static_cast<float>(m_surface->height());
    m_root->setSize(w, h);
    m_bg->setSize(w, h);
    m_bg->setFill(sp::bgPanel(std::max(0.9F, sp::get<float>("panel.opacity", 0.95F))));

    layoutSidebar(renderer);
    m_divider->setPosition(kSidebarW, 0.0F);
    m_divider->setSize(1.0F, h);
    m_divider->setFill(sp::textA(0.06F));

    const float mainX = kSidebarW + 1.0F;
    const float mainW = std::max(1.0F, w - mainX);
    m_main->setPosition(mainX, 0.0F);
    m_main->setSize(mainW, h);

    m_headName->measure(renderer);
    m_headDesc->setMaxWidth(std::max(1.0F, mainW - 36.0F - 64.0F - 20.0F));
    m_headDesc->measure(renderer);
    const float colH = m_headName->height() + 3.0F + m_headDesc->height();
    const float rowH = std::max(48.0F, colH);
    const float tileY = 30.0F + std::round((rowH - 48.0F) / 2.0F);
    m_headTile->setPosition(36.0F, tileY);
    m_headTile->setSize(48.0F, 48.0F);
    verticalGradient(*m_headTile, withAlpha(sp::accent(), 0.32F), withAlpha(sp::accent2(), 0.16F), 15.0F);
    m_headIcon->measure(renderer);
    m_headIcon->setPosition(36.0F + std::round((48.0F - m_headIcon->width()) / 2.0F),
                            tileY + std::round((48.0F - m_headIcon->height()) / 2.0F));
    const float cy = 30.0F + std::round((rowH - colH) / 2.0F);
    m_headName->setPosition(36.0F + 48.0F + 16.0F, cy);
    m_headDesc->setPosition(36.0F + 48.0F + 16.0F, cy + m_headName->height() + 3.0F);

    const float scrollTop = 30.0F + rowH + 24.0F;
    m_scroller->setPosition(0.0F, scrollTop);
    m_scroller->setSize(mainW, std::max(0.0F, h - scrollTop));

    if (m_pageNeedsBuild) buildPage();
    if (m_pageBody != nullptr) {
      const float pageW = std::min(760.0F, mainW - 72.0F);
      m_pageH = m_pageBody->place(renderer, std::max(1.0F, pageW));
      m_pageBody->setPosition(0.0F, 0.0F);
    }
    const float contentH = m_pageH + 40.0F;
    const float maxOff = std::max(0.0F, contentH - m_scroller->height());
    if (m_scroll > maxOff) m_scroll = maxOff;
    if (m_scrollTarget > maxOff) m_scrollTarget = maxOff;
    m_pageHolder->setPosition(36.0F, std::round(m_pageInY - m_scroll));
    m_pageHolder->setSize(std::min(760.0F, mainW - 72.0F), m_pageH);

    const float viewH = m_scroller->height();
    m_indicator->setVisible(contentH > viewH && viewH > 0.0F);
    if (contentH > viewH && viewH > 0.0F) {
      m_indicator->setPosition(mainW - 6.0F, scrollTop + (m_scroll / contentH) * viewH);
      m_indicator->setSize(3.0F, (viewH / contentH) * viewH);
    }
  }

  void SettingsApp::prepareFrame(bool /*needsUpdate*/, bool /*needsLayout*/) {
    if (m_surface == nullptr || m_renderContext == nullptr) return;
    if (m_surface->width() == 0 || m_surface->height() == 0) return;
    m_renderContext->makeCurrent(m_surface->renderTarget());
    Renderer& renderer = m_surface->renderTarget().renderer();
    UiPhaseScope layoutPhase(UiPhase::Layout);
    if (m_sceneDirty || m_root == nullptr) buildScene();
    layoutScene(renderer);
    // An animation started outside the frame loop (a sync, a timer) needs a frame to start ticking.
    if (m_animations.hasActive()) m_surface->requestRedraw();
  }

  void SettingsApp::wakeAfterInput() {
    if (m_root == nullptr || m_surface == nullptr) return;
    if (m_root->layoutDirty()) {
      m_surface->requestLayout();
    } else if (m_root->paintDirty() || m_animations.hasActive()) {
      // A hover tween hasn't changed anything yet but still needs frames.
      m_surface->requestRedraw();
    }
  }

  bool SettingsApp::onPointerEvent(const PointerEvent& event) {
    if (!isOpen()) return false;
    wl_surface* const ws = m_surface->wlSurface();
    const bool onThis = event.surface != nullptr && event.surface == ws;
    bool consumed = false;
    switch (event.type) {
    case PointerEvent::Type::Enter:
      if (onThis) {
        m_pointerInside = true;
        m_dispatcher.pointerEnter(static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial);
        consumed = true;
      }
      break;
    case PointerEvent::Type::Leave:
      if (onThis) {
        m_pointerInside = false;
        m_dispatcher.pointerLeave();
        consumed = true;
      }
      break;
    case PointerEvent::Type::Motion:
      if (onThis || m_pointerInside) {
        m_dispatcher.pointerMotion(static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial);
        consumed = m_pointerInside;
      }
      break;
    case PointerEvent::Type::Button:
      if (onThis || m_pointerInside) {
        m_dispatcher.pointerMotion(static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial);
        m_dispatcher.pointerButton(
            static_cast<float>(event.sx), static_cast<float>(event.sy), event.button, event.pressed, event.serial,
            event.time, event.touch
        );
        consumed = m_pointerInside;
      }
      break;
    case PointerEvent::Type::Axis:
      if (m_pointerInside) {
        m_dispatcher.pointerAxis(
            static_cast<float>(event.sx), static_cast<float>(event.sy), event.axis, event.axisSource, event.axisValue,
            event.axisDiscrete, event.axisValue120, event.axisLines
        );
        consumed = true;
      }
      break;
    }
    wakeAfterInput();
    return consumed;
  }

  void SettingsApp::onKeyboardEvent(const KeyboardEvent& event) {
    if (!isOpen()) return;
    if (event.pressed && KeybindMatcher::matches(KeybindAction::Cancel, event.sym, event.modifiers)) {
      DeferredCall::callLater([this]() { close(); });
      return;
    }
    if (event.pressed && !event.preedit && (event.modifiers & KeyMod::Ctrl) != 0 && (event.sym == XKB_KEY_f || event.sym == XKB_KEY_F)) {
      if (m_search != nullptr) m_dispatcher.setFocus(m_search->focusArea());
      requestRedraw();
      return;
    }
    m_dispatcher.keyEvent(event.sym, event.utf32, event.modifiers, event.pressed, event.preedit);
    wakeAfterInput();
  }

} // namespace kusanagi
