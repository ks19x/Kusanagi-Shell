#include "wayland/LayerSurface.hpp"
#include "app/Log.hpp"
#include "renderer/Gl.hpp"
#include "renderer/Renderer.hpp"
#include "wayland/Display.hpp"
#include "wayland/Output.hpp"
#include "wayland/Seat.hpp"

#include "fractional-scale-v1-client.h"
#include "viewporter-client.h"
#define namespace namespace_   // the protocol names a parameter "namespace"
#include "wlr-layer-shell-unstable-v1-client.h"
#undef namespace

#include <chrono>
#include <cmath>
#include <wayland-client.h>
#include <wayland-egl.h>

namespace ks::wl {

static const zwlr_layer_surface_v1_listener kLayer = {
    .configure = [](void* d, zwlr_layer_surface_v1*, uint32_t serial, uint32_t w, uint32_t h) {
        static_cast<LayerSurface*>(d)->configure(serial, w, h);
    },
    .closed = [](void* d, zwlr_layer_surface_v1*) { static_cast<LayerSurface*>(d)->closed(); },
};

static const wl_callback_listener kFrame = {
    .done = [](void* d, wl_callback*, uint32_t) { static_cast<LayerSurface*>(d)->frameDone(); },
};

static const wp_fractional_scale_v1_listener kFractional = {
    .preferred_scale = [](void* d, wp_fractional_scale_v1*, uint32_t s) { static_cast<LayerSurface*>(d)->setPreferredScale120(s); },
};

static const wl_surface_listener kSurface = {
    .enter = [](void* d, wl_surface*, wl_output* o) {
        // without preferred_buffer_scale (compositor < v6), follow the output we're on
        auto* out = static_cast<Output*>(wl_output_get_user_data(o));
        if (out) static_cast<LayerSurface*>(static_cast<Surface*>(d))->setIntegerScale(out->scale());
    },
    .leave = [](void*, wl_surface*, wl_output*) {},
    .preferred_buffer_scale = [](void* d, wl_surface*, int32_t s) { static_cast<LayerSurface*>(static_cast<Surface*>(d))->setIntegerScale(s); },
    .preferred_buffer_transform = [](void*, wl_surface*, uint32_t) {},
};

LayerSurface::LayerSurface(Display& d, Params p) : m_display(d), m_params(std::move(p)) {
    m_surface = wl_compositor_create_surface(d.compositor);
    // user data is the Surface* (the seat routes pointer events by it); the listener casts back
    wl_surface_add_listener(m_surface, &kSurface, static_cast<Surface*>(this));

    if (d.fractionalScale && d.viewporter) {
        m_fractional = wp_fractional_scale_manager_v1_get_fractional_scale(d.fractionalScale, m_surface);
        wp_fractional_scale_v1_add_listener(m_fractional, &kFractional, this);
        m_viewport = wp_viewporter_get_viewport(d.viewporter, m_surface);
    }
    if (m_params.output) {
        m_intScale = m_params.output->scale();
        m_scale120 = uint32_t(m_intScale * 120);
    }

    m_layer = zwlr_layer_shell_v1_get_layer_surface(d.layerShell, m_surface, m_params.output ? m_params.output->handle() : nullptr,
                                                    m_params.layer, m_params.ns.c_str());
    zwlr_layer_surface_v1_add_listener(m_layer, &kLayer, this);
    zwlr_layer_surface_v1_set_anchor(m_layer, m_params.anchor);
    zwlr_layer_surface_v1_set_size(m_layer, m_params.width, m_params.height);
    zwlr_layer_surface_v1_set_exclusive_zone(m_layer, m_params.exclusiveZone);
    zwlr_layer_surface_v1_set_margin(m_layer, m_params.marginTop, m_params.marginRight, m_params.marginBottom, m_params.marginLeft);
    uint32_t kb = m_params.keyboard;
    if (kb == KbOnDemand && d.layerShellVersion < 4) kb = KbExclusive;
    zwlr_layer_surface_v1_set_keyboard_interactivity(m_layer, kb);
    wl_surface_commit(m_surface);   // bufferless commit → compositor sends configure
}

LayerSurface::~LayerSurface() {
    m_display.forget(this);
    if (auto* seat = m_display.seat()) seat->surfaceGone(this);
    if (m_frame) wl_callback_destroy(m_frame);
    if (m_eglSurface != EGL_NO_SURFACE) gfx::Gl::get().destroySurface(m_eglSurface);
    if (m_eglWindow) wl_egl_window_destroy(m_eglWindow);
    if (m_viewport) wp_viewport_destroy(m_viewport);
    if (m_fractional) wp_fractional_scale_v1_destroy(m_fractional);
    if (m_layer) zwlr_layer_surface_v1_destroy(m_layer);
    if (m_surface) wl_surface_destroy(m_surface);
}

void LayerSurface::setExclusiveZone(int32_t zone) {
    if (zone == m_params.exclusiveZone) return;
    m_params.exclusiveZone = zone;
    zwlr_layer_surface_v1_set_exclusive_zone(m_layer, zone);
}

void LayerSurface::setSize(uint32_t w, uint32_t h) {
    if (w == m_params.width && h == m_params.height) return;
    m_params.width = w;
    m_params.height = h;
    zwlr_layer_surface_v1_set_size(m_layer, w, h);
}

void LayerSurface::setAnchor(uint32_t anchor) {
    if (anchor == m_params.anchor) return;
    m_params.anchor = anchor;
    zwlr_layer_surface_v1_set_anchor(m_layer, anchor);
}

void LayerSurface::setMargins(int32_t t, int32_t r, int32_t b, int32_t l) {
    m_params.marginTop = t;
    m_params.marginRight = r;
    m_params.marginBottom = b;
    m_params.marginLeft = l;
    zwlr_layer_surface_v1_set_margin(m_layer, t, r, b, l);
}

void LayerSurface::setInputRegion(const std::vector<RectI>& rects) {
    wl_region* r = wl_compositor_create_region(m_display.compositor);
    for (auto& rc : rects) wl_region_add(r, rc.x, rc.y, rc.w, rc.h);
    wl_surface_set_input_region(m_surface, r);
    wl_region_destroy(r);
}

void LayerSurface::setInputFull() { wl_surface_set_input_region(m_surface, nullptr); }

void LayerSurface::commit() {
    if (m_configured) requestRedraw();   // the redraw's swap commits pending state
    else wl_surface_commit(m_surface);
}

void LayerSurface::configure(uint32_t serial, uint32_t w, uint32_t h) {
    zwlr_layer_surface_v1_ack_configure(m_layer, serial);
    bool resized = int(w) != m_w || int(h) != m_h;
    m_w = int(w);
    m_h = int(h);
    m_configured = true;
    updateBufferSize();
    if (resized && onResize) onResize(m_w, m_h);
    requestRedraw();
}

void LayerSurface::closed() {
    if (onClosed) onClosed();
}

void LayerSurface::setPreferredScale120(uint32_t s) {
    if (s == m_scale120 || s == 0) return;
    m_scale120 = s;
    updateBufferSize();
    if (onScaleChanged) onScaleChanged();
    requestRedraw();
}

void LayerSurface::setIntegerScale(int s) {
    if (m_fractional || s <= 0 || s == m_intScale) return;
    m_intScale = s;
    m_scale120 = uint32_t(s * 120);
    updateBufferSize();
    if (onScaleChanged) onScaleChanged();
    requestRedraw();
}

void LayerSurface::updateBufferSize() {
    if (m_w <= 0 || m_h <= 0) return;
    if (m_viewport) {
        m_bw = int(std::lround(m_w * m_scale120 / 120.0));
        m_bh = int(std::lround(m_h * m_scale120 / 120.0));
        wp_viewport_set_destination(m_viewport, m_w, m_h);
    } else {
        m_bw = m_w * m_intScale;
        m_bh = m_h * m_intScale;
        wl_surface_set_buffer_scale(m_surface, m_intScale);
    }
    if (!m_eglWindow) {
        m_eglWindow = wl_egl_window_create(m_surface, m_bw, m_bh);
        m_eglSurface = gfx::Gl::get().createSurface(m_eglWindow);
    } else {
        wl_egl_window_resize(m_eglWindow, m_bw, m_bh, 0, 0);
    }
}

void LayerSurface::requestRedraw() {
    m_dirty = true;
    if (!m_frame) m_display.markDirty(this);   // otherwise the frame callback picks it up
}

void LayerSurface::drawIfReady() {
    if (m_configured && m_dirty && !m_frame) draw();
}

void LayerSurface::frameDone() {
    wl_callback_destroy(m_frame);
    m_frame = nullptr;
    if (m_dirty) m_display.markDirty(this);
}

void LayerSurface::draw() {
    if (m_eglSurface == EGL_NO_SURFACE || m_bw <= 0) return;
    log::debug("surface", "draw {} {}x{} buf {}x{}", m_params.ns, m_w, m_h, m_bw, m_bh);
    m_dirty = false;
    double now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    bool animating = onTick ? onTick(now) : false;

    auto& gl = gfx::Gl::get();
    gl.makeCurrent(m_eglSurface);
    auto& r = gfx::Renderer::get();
    r.beginFrame(m_bw, m_bh, float(m_bw) / float(m_w));
    if (onRender) onRender(r);
    r.endFrame();

    m_frame = wl_surface_frame(m_surface);
    wl_callback_add_listener(m_frame, &kFrame, this);
    wl_surface_damage_buffer(m_surface, 0, 0, INT32_MAX, INT32_MAX);
    gl.swap(m_eglSurface);
    if (animating) m_dirty = true;   // frameDone schedules the next one
}

} // namespace ks::wl
