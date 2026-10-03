#include "wayland/Output.hpp"
#include "wayland/Display.hpp"

#include "xdg-output-unstable-v1-client.h"

#include <wayland-client.h>

namespace ks::wl {

static const wl_output_listener kOutput = {
    .geometry = [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {},
    .mode = [](void* d, wl_output*, uint32_t flags, int32_t w, int32_t h, int32_t) {
        if (flags & WL_OUTPUT_MODE_CURRENT) static_cast<Output*>(d)->setMode(w, h);
    },
    .done = [](void* d, wl_output*) { static_cast<Output*>(d)->done(); },
    .scale = [](void* d, wl_output*, int32_t s) { static_cast<Output*>(d)->setScale(s); },
    .name = [](void* d, wl_output*, const char* n) { static_cast<Output*>(d)->setName(n); },
    .description = [](void* d, wl_output*, const char* s) { static_cast<Output*>(d)->setDescription(s); },
};

static const zxdg_output_v1_listener kXdgOutput = {
    .logical_position = [](void* d, zxdg_output_v1*, int32_t x, int32_t y) { static_cast<Output*>(d)->setLogicalPos(x, y); },
    .logical_size = [](void* d, zxdg_output_v1*, int32_t w, int32_t h) { static_cast<Output*>(d)->setLogicalSize(w, h); },
    .done = [](void*, zxdg_output_v1*) {},   // deprecated since v3: wl_output.done covers it
    .name = [](void* d, zxdg_output_v1*, const char* n) {
        auto* o = static_cast<Output*>(d);
        if (o->name().empty()) o->setName(n);
    },
    .description = [](void*, zxdg_output_v1*, const char*) {},
};

Output::Output(Display* d, wl_output* o, uint32_t globalName) : m_display(d), m_output(o), m_global(globalName) {
    wl_output_add_listener(m_output, &kOutput, this);
    wl_output_set_user_data(m_output, this);
}

Output::~Output() {
    if (m_xdg) zxdg_output_v1_destroy(m_xdg);
    if (wl_output_get_version(m_output) >= 3) wl_output_release(m_output);
    else wl_output_destroy(m_output);
}

void Output::attachXdgOutput(zxdg_output_manager_v1* mgr) {
    if (m_xdg || !mgr) return;
    m_xdg = zxdg_output_manager_v1_get_xdg_output(mgr, m_output);
    zxdg_output_v1_add_listener(m_xdg, &kXdgOutput, this);
}

void Output::done() {
    if (!m_announced) {
        if (m_modeW == 0 && m_lw == 0) return;   // not enough info yet
        m_announced = true;
        m_display->outputReady(this);
        return;
    }
    if (onChanged) onChanged();
}

} // namespace ks::wl
