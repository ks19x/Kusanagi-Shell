#include "ui/Widget.hpp"

#include <algorithm>

namespace ks::ui {

Widget::Widget() {
    track(opacity);
    track(scale);
}

Widget::~Widget() {
    if (m_root && m_root != this) {
        // drop dangling hover / press references
        m_root->m_hoverChain.erase(std::remove(m_root->m_hoverChain.begin(), m_root->m_hoverChain.end(), this), m_root->m_hoverChain.end());
        if (m_root->m_pressed == this) m_root->m_pressed = nullptr;
    }
    m_children.clear();
}

void Widget::track(anim::Base& v) {
    m_tracked.push_back(&v);
    if (m_root) v.attach(&m_root->clock);
}

void Widget::setRoot(Root* r) {
    m_root = r;
    for (auto* v : m_tracked) v->attach(r ? &r->clock : nullptr);
    for (auto& c : m_children) c->setRoot(r);
}

void Widget::adopt(std::unique_ptr<Widget> w) {
    w->m_parent = this;
    w->setRoot(m_root);
    m_children.push_back(std::move(w));
    relayout();
}

void Widget::remove(Widget* w) {
    auto it = std::find_if(m_children.begin(), m_children.end(), [&](auto& c) { return c.get() == w; });
    if (it != m_children.end()) {
        m_children.erase(it);
        relayout();
    }
}

void Widget::clear() {
    m_children.clear();
    relayout();
}

void Widget::update() {
    if (m_root && m_root->requestFrame) m_root->requestFrame();
}

void Widget::relayout() {
    if (m_root) m_root->needLayout();
}

void Widget::paint(gfx::Renderer& r) {
    if (!visible) return;
    float op = opacity.get();
    if (op <= 0.001f) return;
    r.save();
    r.translate(x, y);
    if (op < 1) r.multiplyOpacity(op);
    float s = scale.get();
    if (s != 1.f) r.scaleAbout(s, w / 2, h / 2);
    if (clip) r.pushClip({0, 0, w, h});
    paintSelf(r);
    if (onPaint) onPaint(*this, r);
    for (auto& c : m_children) c->paint(r);
    if (clip) r.popClip();
    r.restore();
}

Widget* Widget::hitTest(float lx, float ly) {
    if (!visible || opacity.get() <= 0.001f) return nullptr;
    // children were painted last-on-top, so test them in reverse
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        auto& c = *it;
        if (Widget* hit = c->hitTest(lx - c->x, ly - c->y)) return hit;
    }
    return contains(lx, ly) ? this : nullptr;
}

void Widget::mapFromRoot(float& px, float& py) const {
    for (const Widget* w = this; w && w != m_root; w = w->m_parent) {
        px -= w->x;
        py -= w->y;
    }
}

// ---------------------------------------------------------------- Root

Root::Root() {
    m_root = this;
    for (auto* v : m_tracked) v->attach(&clock);
    clock.wake = [this] {
        if (requestFrame) requestFrame();
    };
}

Root::~Root() {
    m_hoverChain.clear();
    m_pressed = nullptr;
    m_children.clear();
}

void Root::layoutTree(Widget* w) {
    if (!w->visible) return;
    w->layout();
    if (w->onLayout) w->onLayout(*w);
    for (auto& c : w->m_children) layoutTree(c.get());
}

bool Root::tick(double now) {
    bool running = clock.tick(now);
    // animations move geometry too (slot widths…), so lay out every animating frame
    if (running || m_layoutDirty) {
        m_layoutDirty = false;
        layoutTree(this);
        if (m_px >= 0) pointerMotion(m_px, m_py);   // things moved under a still pointer
    }
    return running;
}

void Root::render(gfx::Renderer& r) {
    for (auto& c : m_children) c->paint(r);
}

void Root::setHoverChain(Widget* deepest) {
    std::vector<Widget*> chain;
    for (Widget* w = deepest; w && w != this; w = w->m_parent)
        if (w->hoverable) chain.push_back(w);
    for (Widget* old : m_hoverChain)
        if (std::find(chain.begin(), chain.end(), old) == chain.end()) {
            old->hovered = false;
            if (old->onHover) old->onHover(false);
            old->update();
        }
    for (Widget* nw : chain)
        if (std::find(m_hoverChain.begin(), m_hoverChain.end(), nw) == m_hoverChain.end()) {
            nw->hovered = true;
            if (nw->onHover) nw->onHover(true);
            nw->update();
        }
    m_hoverChain = std::move(chain);
    wl::Cursor c = wl::Cursor::Default;
    for (Widget* w = deepest; w && w != this; w = w->m_parent)
        if (w->cursor != wl::Cursor::Default) { c = w->cursor; break; }
    if (setCursor) setCursor(c);
}

void Root::pointerMotion(float px, float py) {
    m_px = px;
    m_py = py;
    Widget* hit = nullptr;
    for (auto it = m_children.rbegin(); it != m_children.rend() && !hit; ++it) hit = (*it)->hitTest(px - (*it)->x, py - (*it)->y);
    setHoverChain(hit);
}

void Root::pointerLeave() {
    m_px = m_py = -1;
    setHoverChain(nullptr);
    m_pressed = nullptr;
}

void Root::pointerButton(int button, bool pressed) {
    Widget* hit = nullptr;
    for (auto it = m_children.rbegin(); it != m_children.rend() && !hit; ++it) hit = (*it)->hitTest(m_px - (*it)->x, m_py - (*it)->y);
    if (pressed) {
        m_pressed = hit;
        m_pressedButton = button;
        return;
    }
    // click = release over the widget (or a descendant of it) that got the press
    Widget* target = m_pressed;
    m_pressed = nullptr;
    if (!target || button != m_pressedButton) return;
    bool inside = false;
    for (Widget* w = hit; w; w = w->m_parent)
        if (w == target) { inside = true; break; }
    if (!inside) return;
    for (Widget* w = target; w && w != this; w = w->m_parent)
        if (w->onClick && w->onClick(button)) return;
}

void Root::pointerScroll(int steps) {
    Widget* hit = nullptr;
    for (auto it = m_children.rbegin(); it != m_children.rend() && !hit; ++it) hit = (*it)->hitTest(m_px - (*it)->x, m_py - (*it)->y);
    for (Widget* w = hit; w && w != this; w = w->m_parent)
        if (w->onScroll && w->onScroll(steps)) return;
}

} // namespace ks::ui
