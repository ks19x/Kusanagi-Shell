// Seat.hpp — pointer input routed to the Surface under it (keyboard arrives with the overlays, M4).
// Wheel input is normalised to ±1 steps like Qt's angleDelta/120: positive = scroll up / away.
#pragma once
#include <cstdint>

struct wl_seat;
struct wl_pointer;
struct wl_surface;
struct wp_cursor_shape_device_v1;

namespace ks::wl {

class Display;
class Surface;

enum class Cursor { Default, Pointer, Text };

class Seat {
public:
    Seat(Display* d, wl_seat* s);
    ~Seat();

    void setCursor(Cursor c);
    Surface* pointerFocus() const { return m_focus; }
    void surfaceGone(Surface* s) { if (m_focus == s) m_focus = nullptr; }

    // listener plumbing
    void capabilities(uint32_t caps);
    void enter(uint32_t serial, wl_surface* s, double x, double y);
    void leave(wl_surface* s);
    void motion(double x, double y);
    void button(uint32_t serial, uint32_t button, uint32_t state);
    void axis(uint32_t axis, double value);
    void axisDiscrete(uint32_t axis, int32_t steps120);
    void frame();

private:
    Display* m_display;
    wl_seat* m_seat;
    wl_pointer* m_pointer = nullptr;
    wp_cursor_shape_device_v1* m_shape = nullptr;
    Surface* m_focus = nullptr;
    uint32_t m_enterSerial = 0;
    Cursor m_cursor = Cursor::Default;
    double m_x = 0, m_y = 0;
    double m_axisPx = 0;      // continuous scroll accumulated this frame
    int m_axis120 = 0;        // wheel clicks (×120) this frame
    bool m_sawDiscrete = false;
    double m_smoothAcc = 0;   // touchpad: accumulate until a "click" worth
};

} // namespace ks::wl
