#pragma once

// Kusanagi Settings window: a sidebar with search and the page list, and the selected page on the right.
// Pages come from the table in sp_pages.cpp and every control writes settings.json, which the shell
// reloads live. The scene is torn down while the window is closed.

#include "render/animation/animation_manager.h"
#include "render/scene/input_dispatcher.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Box;
class Image;
class InputArea;
class IpcService;
class Label;
class Node;
class Renderer;
class RenderContext;
class ToplevelSurface;
class WaylandConnection;
struct KeyboardEvent;
struct PointerEvent;
struct wl_output;
struct wl_surface;

namespace kusanagi {

  namespace sp {
    class Column;
    class Field;
    struct PageInfo;
  } // namespace sp

  class SettingsApp {
  public:
    static SettingsApp& instance();

    void initialize(WaylandConnection& wayland, RenderContext* renderContext, IpcService* ipc);
    void registerIpc(IpcService& ipc);

    // Shows the window on `page`. An empty id keeps the current page (or the first one).
    void open(const std::string& page = {});
    void toggle(const std::string& page = {});
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return m_surface != nullptr; }

    [[nodiscard]] bool ownsKeyboardSurface(wl_surface* surface) const noexcept;
    [[nodiscard]] bool onPointerEvent(const PointerEvent& event);
    void onKeyboardEvent(const KeyboardEvent& event);
    void onConfigReload();

  private:
    struct NavItem;

    void destroyWindow();
    void prepareFrame(bool needsUpdate, bool needsLayout);
    void buildScene();
    void buildPage();
    void layoutScene(Renderer& renderer);
    void layoutSidebar(Renderer& renderer);
    void selectPage(const std::string& id);
    void applySearch(const std::string& query);
    void scrollPageTo(float offset, bool animate);
    void moveHighlight(bool animate);
    void requestLayout();
    void requestRedraw();
    void wakeAfterInput();
    void onSettingsChanged();
    [[nodiscard]] const sp::PageInfo& currentPage() const;
    [[nodiscard]] std::vector<const sp::PageInfo*> shownPages() const;

    WaylandConnection* m_wayland = nullptr;
    RenderContext* m_renderContext = nullptr;
    IpcService* m_ipc = nullptr;

    // m_root must be destroyed before m_animations because ~Node cancels its animations.
    AnimationManager m_animations;
    std::unique_ptr<ToplevelSurface> m_surface;
    std::unique_ptr<Node> m_root;
    InputDispatcher m_dispatcher;
    wl_output* m_output = nullptr;
    bool m_pointerInside = false;
    bool m_sceneDirty = true;
    std::string m_builtFont;

    std::string m_page;
    std::string m_query;

    Box* m_bg = nullptr;
    Box* m_sidebar = nullptr;
    Box* m_divider = nullptr;
    Image* m_logo = nullptr;
    bool m_logoLoaded = false;
    Label* m_brand = nullptr;
    Label* m_brandSub = nullptr;
    sp::Field* m_search = nullptr;
    InputArea* m_navView = nullptr;
    Node* m_navContent = nullptr;
    Box* m_highlight = nullptr;
    std::vector<NavItem*> m_nav;
    float m_navScroll = 0.0F;
    float m_navContentH = 0.0F;
    float m_highlightY = 0.0F;
    float m_highlightH = 0.0F;
    bool m_highlightPlaced = false;

    Node* m_main = nullptr;
    Box* m_headTile = nullptr;
    Label* m_headIcon = nullptr;
    Label* m_headName = nullptr;
    Label* m_headDesc = nullptr;
    InputArea* m_scroller = nullptr;
    Node* m_pageHolder = nullptr;
    sp::Column* m_pageBody = nullptr;
    Box* m_indicator = nullptr;
    float m_scroll = 0.0F;
    float m_scrollTarget = 0.0F;
    float m_pageH = 0.0F;
    float m_pageInY = 0.0F; // Slide-in offset of the page.
    bool m_pageNeedsBuild = true;
    bool m_keepScroll = false; // Rebuild for a new font: keep the scroll position and skip the slide-in.
  };

} // namespace kusanagi
