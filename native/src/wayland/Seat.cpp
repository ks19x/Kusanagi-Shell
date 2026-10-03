#include "wayland/Seat.hpp"
#include "wayland/Display.hpp"
#include "wayland/LayerSurface.hpp"

#include "cursor-shape-v1-client.h"

#include <cmath>
#include <wayland-client.h>

namespace ks::wl {

static const wl_seat_listener kSeat = {
    .capabilities = [](void* d, wl_seat*, uint32_t caps) { static_cast<Seat*>(d)->capabilities(caps); },
    .name = [](void*, wl_seat*, const char*) {},
};

static const wl_pointer_listener kPointer = {
    .enter = [](void* d, wl_pointer*, uint32_t serial, wl_surface* s, wl_fixed_t x, wl_fixed_t y) {
        static_cast<Seat*>(d)->enter(serial, s, wl_fixed_to_double(x), wl_fixed_to_double(y));
    },
    .leave = [](void* d, wl_pointer*, uint32_t, wl_surface* s) { static_cast<Seat*>(d)->leave(s); },
    .motion = [](void* d, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
        static_cast<Seat*>(d)->motion(wl_fixed_to_double(x), wl_fixed_to_double(y));
    },
    .button = [](void* d, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) {
        static_cast<Seat*>(d)->button(serial, button, state);
    },
    .axis = [](void* d, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t v) { static_cast<Seat*>(d)->axis(axis, wl_fixed_to_double(v)); },
    .frame = [](void* d, wl_pointer*) { static_cast<Seat*>(d)->frame(); },
    .axis_source = [](void*, wl_pointer*, uint32_t) {},
    .axis_stop = [](void*, wl_pointer*, uint32_t, uint32_t) {},
    .axis_discrete = [](void* d, wl_pointer*, uint32_t axis, int32_t steps) { static_cast<Seat*>(d)->axisDiscrete(axis, steps * 120); },
    .axis_value120 = [](void* d, wl_pointer*, uint32_t axis, int32_t v120) { static_cast<Seat*>(d)->axisDiscrete(axis, v120); },
    .axis_relative_direction = [](void*, wl_pointer*, uint32_t, uint32_t) {},
};

Seat::Seat(Display* d, wl_seat* s) : m_display(d), m_seat(s) { wl_seat_add_listener(m_seat, &kSeat, this); }

Seat::~Seat() {
    if (m_shape) wp_cursor_shape_device_v1_destroy(m_shape);
    if (m_pointer) wl_pointer_release(m_pointer);
    wl_seat_release(m_seat);
}

void Seat::capabilities(uint32_t caps) {
    bool hasPointer = caps & WL_SEAT_CAPABILITY_POINTER;
    if (hasPointer && !m_pointer) {
        m_pointer = wl_seat_get_pointer(m_seat);
        wl_pointer_add_listener(m_pointer, &kPointer, this);
        if (m_display->cursorShape) m_shape = wp_cursor_shape_manager_v1_get_pointer(m_display->cursorShape, m_pointer);
    } else if (!hasPointer && m_pointer) {
        if (m_shape) wp_cursor_shape_device_v1_destroy(m_shape);
        m_shape = nullptr;
        wl_pointer_release(m_pointer);
        m_pointer = nullptr;
    }
}

void Seat::setCursor(Cursor c) {
    if (!m_shape || !m_focus || c == m_cursor) return;
    m_cursor = c;
    uint32_t shape = c == Cursor::Pointer ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER
                   : c == Cursor::Text  ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT
                                        : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    wp_cursor_shape_device_v1_set_shape(m_shape, m_enterSerial, shape);
}

void Seat::enter(uint32_t serial, wl_surface* s, double x, double y) {
    m_enterSerial = serial;
    m_focus = s ? static_cast<Surface*>(wl_surface_get_user_data(s)) : nullptr;
    m_x = x;
    m_y = y;
    m_cursor = Cursor::Pointer;     // force the next setCursor to send
    setCursor(Cursor::Default);
    if (m_focus) m_focus->pointerMotion(x, y);
}

void Seat::leave(wl_surface*) {
    if (m_focus) m_focus->pointerLeave();
    m_focus = nullptr;
}

void Seat::motion(double x, double y) {
    m_x = x;
    m_y = y;
    if (m_focus) m_focus->pointerMotion(x, y);
}

void Seat::button(uint32_t, uint32_t button, uint32_t state) {
    if (m_focus) m_focus->pointerButton(button, state == WL_POINTER_BUTTON_STATE_PRESSED);
}

void Seat::axis(uint32_t axis, double value) {
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) m_axisPx += value;
}

void Seat::axisDiscrete(uint32_t axis, int32_t steps120) {
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
    m_axis120 += steps120;
    m_sawDiscrete = true;
}

void Seat::frame() {
    int steps = 0;
    if (m_sawDiscrete) {
        // wheel: Wayland's positive = down; Kusanagi's (Qt's) positive = up
        steps = -m_axis120 / 120;
        if (steps == 0 && m_axis120 != 0) steps = m_axis120 > 0 ? -1 : 1;   // hi-res wheels: count partial clicks
        m_smoothAcc = 0;
    } else if (m_axisPx != 0) {
        m_smoothAcc += m_axisPx;           // touchpad: one step per 15 px (Qt's 120 angle units)
        while (std::fabs(m_smoothAcc) >= 15) {
            steps += m_smoothAcc > 0 ? -1 : 1;
            m_smoothAcc += m_smoothAcc > 0 ? -15 : 15;
        }
    }
    m_axis120 = 0;
    m_axisPx = 0;
    m_sawDiscrete = false;
    if (steps && m_focus) m_focus->pointerScroll(steps);
}

} // namespace ks::wl
