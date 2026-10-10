#pragma once

// First-run setup window. Most steps reuse the Settings page builders from sp_pages.cpp, so anything picked
// here can be changed later in Settings. It opens on its own shortly after the first start (no settings.json)
// and on demand with `kusanagi-setup open|toggle|hide`.

#include "core/timer_manager.h"
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
struct wl_surface;

namespace kusanagi {

  namespace sp {
    class Column;
    class Chip;
  } // namespace sp

  class SetupApp {
  public:
    static SetupApp& instance();

    void initialize(WaylandConnection& wayland, RenderContext* renderContext, IpcService* ipc);
    void registerIpc(IpcService& ipc);
    // With no settings.json yet, writes the defaults and opens the setup a moment later.
    void checkFirstRun();

    // Shows the window on the first step, or raises it if it's already open.
    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return m_surface != nullptr; }

    [[nodiscard]] bool ownsKeyboardSurface(wl_surface* surface) const noexcept;
    [[nodiscard]] bool onPointerEvent(const PointerEvent& event);
    void onKeyboardEvent(const KeyboardEvent& event);
    void onConfigReload();

  private:
    struct StepItem;

    void destroyWindow();
    void prepareFrame();
    void buildScene();
    void buildContent();
    void layoutScene(Renderer& renderer);
    void goTo(int step);
    void scrollTo(float offset, bool animate);
    void requestLayout();
    void wakeAfterInput();
    void onSettingsChanged();
    void readMissing();

    WaylandConnection* m_wayland = nullptr;
    RenderContext* m_renderContext = nullptr;
    IpcService* m_ipc = nullptr;

    // m_root must be destroyed before m_animations because ~Node cancels its animations.
    AnimationManager m_animations;
    std::unique_ptr<ToplevelSurface> m_surface;
    std::unique_ptr<Node> m_root;
    InputDispatcher m_dispatcher;
    bool m_pointerInside = false;
    bool m_sceneDirty = true;
    std::string m_builtFont;
    Timer m_firstRun;

    int m_step = 0;
    // Items `kusanagi doctor` reports as missing.
    std::shared_ptr<std::vector<std::string>> m_missing = std::make_shared<std::vector<std::string>>();

    // Sidebar
    Box* m_bg = nullptr;
    Box* m_side = nullptr;
    Box* m_divider = nullptr;
    Image* m_logo = nullptr;
    bool m_logoLoaded = false;
    Label* m_brand = nullptr;
    Label* m_brandSub = nullptr;
    std::vector<StepItem*> m_steps;

    // Current step
    Node* m_main = nullptr;
    Label* m_headName = nullptr;
    Label* m_headDesc = nullptr;
    InputArea* m_scroller = nullptr;
    Node* m_holder = nullptr;
    sp::Column* m_content = nullptr;
    float m_scroll = 0.0F;
    float m_scrollTarget = 0.0F;
    float m_contentH = 0.0F;
    bool m_contentNeedsBuild = true;
    bool m_keepScroll = false;

    // Footer
    Box* m_footer = nullptr;
    Box* m_footerLine = nullptr;
    sp::Chip* m_skip = nullptr;
    sp::Chip* m_back = nullptr;
    sp::Chip* m_next = nullptr;   // "Start" on the first step, "Next" after.
    sp::Chip* m_finish = nullptr;
  };

} // namespace kusanagi
