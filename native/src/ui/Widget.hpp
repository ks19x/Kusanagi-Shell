// Widget.hpp — Kusanagi's retained widget tree (not a Qt clone: only what the shell needs).
// Geometry is logical px relative to the parent. A Root sits on one surface: it owns the
// animation clock, routes pointer input (hover chain, click/scroll bubble up) and asks the
// surface for frames. Animated properties are anim::Value<T>s registered with track().
#pragma once
#include "anim/Animation.hpp"
#include "renderer/Renderer.hpp"
#include "wayland/Seat.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace ks::ui {

class Root;

enum Button { Left = 1, Right = 2, Middle = 3 };

class Widget {
    friend class Root;
public:
    Widget();
    virtual ~Widget();
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    // ---- tree
    template <class T, class... A> T* add(A&&... a) {
        auto w = std::make_unique<T>(std::forward<A>(a)...);
        T* raw = w.get();
        adopt(std::move(w));
        return raw;
    }
    void adopt(std::unique_ptr<Widget> w);
    void remove(Widget* w);
    void clear();
    Widget* parent() const { return m_parent; }
    Root* root() const { return m_root; }
    const std::vector<std::unique_ptr<Widget>>& children() const { return m_children; }

    // ---- geometry
    float x = 0, y = 0, w = 0, h = 0;
    void setGeometry(float nx, float ny, float nw, float nh) { x = nx; y = ny; w = nw; h = nh; }
    virtual float implicitWidth() { return w; }
    virtual float implicitHeight() { return h; }
    virtual void layout() {}                      // place children; called top-down every frame that's dirty
    std::function<void(Widget&)> onLayout;        // or supply it inline

    // ---- painting
    bool visible = true;
    anim::Value<float> opacity{1.f};
    anim::Value<float> scale{1.f};                // about the centre
    bool clip = false;
    virtual void paint(gfx::Renderer& r);
    virtual void paintSelf(gfx::Renderer& r) {}
    std::function<void(Widget&, gfx::Renderer&)> onPaint;

    // ---- input
    bool hoverable = false;                       // takes part in the hover chain
    bool hovered = false;
    wl::Cursor cursor = wl::Cursor::Default;
    std::function<void(bool)> onHover;
    std::function<bool(int button)> onClick;      // return false to let it bubble
    std::function<bool(int steps)> onScroll;
    virtual bool contains(float lx, float ly) const { return lx >= 0 && ly >= 0 && lx < w && ly < h; }
    Widget* hitTest(float lx, float ly);          // deepest visible widget under the point
    void mapFromRoot(float& px, float& py) const;

    // ---- invalidation
    void update();                                // repaint
    void relayout();                              // re-run layout + repaint

    void track(anim::Base& v);                    // attach to the root's clock when in a tree

protected:
    void setRoot(Root* r);
    Widget* m_parent = nullptr;
    Root* m_root = nullptr;
    std::vector<std::unique_ptr<Widget>> m_children;
    std::vector<anim::Base*> m_tracked;
    friend class Root;
};

class Root : public Widget {
    friend class Widget;
public:
    Root();
    ~Root() override;

    float deviceScale = 1;
    anim::Clock clock;
    std::function<void()> requestFrame;
    std::function<void(wl::Cursor)> setCursor;

    bool tick(double now);                        // returns true while animating
    void render(gfx::Renderer& r);
    void needLayout() { m_layoutDirty = true; if (requestFrame) requestFrame(); }

    void pointerMotion(float px, float py);
    void pointerLeave();
    void pointerButton(int button, bool pressed);
    void pointerScroll(int steps);

private:
    void setHoverChain(Widget* deepest);
    void layoutTree(Widget* w);
    bool m_layoutDirty = true;
    float m_px = -1, m_py = -1;
    std::vector<Widget*> m_hoverChain;
    Widget* m_pressed = nullptr;
    int m_pressedButton = 0;
};

} // namespace ks::ui
