#include "wayland/Display.hpp"
#include "app/EventLoop.hpp"
#include "app/Log.hpp"
#include "wayland/LayerSurface.hpp"
#include "wayland/Output.hpp"
#include "wayland/Seat.hpp"

#include "cursor-shape-v1-client.h"
#include "fractional-scale-v1-client.h"
#include "viewporter-client.h"
#define namespace namespace_   // the protocol names a parameter "namespace"
#include "wlr-layer-shell-unstable-v1-client.h"
#undef namespace
#include "xdg-output-unstable-v1-client.h"
#include "xdg-shell-client.h"

#include <algorithm>
#include <cstring>
#include <sys/epoll.h>

namespace ks::wl {

static Display* g_display = nullptr;
Display& Display::get() { return *g_display; }

static const wl_registry_listener kRegistry = {
    .global = [](void* d, wl_registry*, uint32_t name, const char* iface, uint32_t v) { static_cast<Display*>(d)->global(name, iface, v); },
    .global_remove = [](void* d, wl_registry*, uint32_t name) { static_cast<Display*>(d)->globalRemove(name); },
};

static const xdg_wm_base_listener kWmBase = {
    .ping = [](void*, xdg_wm_base* b, uint32_t serial) { xdg_wm_base_pong(b, serial); },
};

Display::Display() { g_display = this; }

Display::~Display() {
    m_seat.reset();
    m_outputs.clear();
    if (m_display) wl_display_disconnect(m_display);
    g_display = nullptr;
}

bool Display::connect() {
    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        log::error("wayland", "can't connect to the compositor (WAYLAND_DISPLAY unset?)");
        return false;
    }
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &kRegistry, this);
    wl_display_roundtrip(m_display);   // globals
    wl_display_roundtrip(m_display);   // output / seat details
    if (!compositor || !layerShell) {
        log::error("wayland", "the compositor lacks {}", !compositor ? "wl_compositor" : "wlr-layer-shell");
        return false;
    }

    auto& loop = EventLoop::main();
    loop.watch(wl_display_get_fd(m_display), EPOLLIN, [this](uint32_t ev) {
        if (ev & (EPOLLERR | EPOLLHUP)) {
            log::error("wayland", "compositor connection lost");
            EventLoop::main().quit(1);
            return;
        }
        if (wl_display_dispatch(m_display) < 0) {
            log::error("wayland", "dispatch failed: {}", strerror(errno));
            EventLoop::main().quit(1);
        }
    });
    loop.beforeWait([this] {
        wl_display_dispatch_pending(m_display);
        flushDirty();
        wl_display_flush(m_display);
    });
    return true;
}

void Display::flushDirty() {
    if (m_dirty.empty()) return;
    auto dirty = std::move(m_dirty);
    m_dirty.clear();
    for (Surface* s : dirty) s->drawIfReady();
}

void* Display::bind(const wl_interface* iface, uint32_t maxVersion, uint32_t* version) {
    auto it = m_globals.find(iface->name);
    if (it == m_globals.end()) return nullptr;
    uint32_t v = std::min(it->second.version, std::min(maxVersion, uint32_t(iface->version)));
    if (version) *version = v;
    return wl_registry_bind(m_registry, it->second.name, iface, v);
}

void Display::global(uint32_t name, const char* iface, uint32_t version) {
    std::string_view i = iface;
    m_globals[std::string(i)] = {name, version};
    auto b = [&](const wl_interface* in, uint32_t maxV) {
        return wl_registry_bind(m_registry, name, in, std::min({version, maxV, uint32_t(in->version)}));
    };
    if (i == wl_compositor_interface.name) {
        compositorVersion = std::min(version, 6u);
        compositor = static_cast<wl_compositor*>(b(&wl_compositor_interface, 6));
    } else if (i == zwlr_layer_shell_v1_interface.name) {
        layerShellVersion = std::min(version, 5u);
        layerShell = static_cast<zwlr_layer_shell_v1*>(b(&zwlr_layer_shell_v1_interface, 5));
    } else if (i == xdg_wm_base_interface.name) {
        wmBase = static_cast<xdg_wm_base*>(b(&xdg_wm_base_interface, 6));
        xdg_wm_base_add_listener(wmBase, &kWmBase, this);
    } else if (i == wp_viewporter_interface.name) {
        viewporter = static_cast<wp_viewporter*>(b(&wp_viewporter_interface, 1));
    } else if (i == wp_fractional_scale_manager_v1_interface.name) {
        fractionalScale = static_cast<wp_fractional_scale_manager_v1*>(b(&wp_fractional_scale_manager_v1_interface, 1));
    } else if (i == wp_cursor_shape_manager_v1_interface.name) {
        cursorShape = static_cast<wp_cursor_shape_manager_v1*>(b(&wp_cursor_shape_manager_v1_interface, 1));
    } else if (i == zxdg_output_manager_v1_interface.name) {
        xdgOutput = static_cast<zxdg_output_manager_v1*>(b(&zxdg_output_manager_v1_interface, 3));
        for (auto& o : m_outputs) o->attachXdgOutput(xdgOutput);
    } else if (i == wl_output_interface.name) {
        auto* wo = static_cast<wl_output*>(b(&wl_output_interface, 4));
        auto o = std::make_unique<Output>(this, wo, name);
        if (xdgOutput) o->attachXdgOutput(xdgOutput);
        m_outputs.push_back(std::move(o));
    } else if (i == wl_seat_interface.name && !m_seat) {
        auto* ws = static_cast<wl_seat*>(b(&wl_seat_interface, 8));
        m_seat = std::make_unique<Seat>(this, ws);
    }
}

void Display::globalRemove(uint32_t name) {
    auto it = std::find_if(m_outputs.begin(), m_outputs.end(), [&](auto& o) { return o->globalName() == name; });
    if (it != m_outputs.end()) {
        log::info("wayland", "output {} removed", (*it)->name());
        if ((*it)->ready() && onOutputRemoved) onOutputRemoved(it->get());
        m_outputs.erase(it);
    }
    for (auto g = m_globals.begin(); g != m_globals.end(); ++g)
        if (g->second.name == name) {
            m_globals.erase(g);
            break;
        }
}

void Display::outputReady(Output* o) {
    log::info("wayland", "output {} ({}) {}x{} @{} scale {}", o->name(), o->description(), o->width(), o->height(), o->x(), o->scale());
    if (onOutputAdded) onOutputAdded(o);
}

} // namespace ks::wl
