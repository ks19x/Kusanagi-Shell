// LayerSurface.hpp — a wl_surface with an EGL window, drawn on demand:
//   requestRedraw() marks it dirty; the Display draws dirty surfaces right before the loop sleeps,
//   and never more than once per frame callback. While `onTick` reports running animations the
//   surface keeps asking for frames; when idle it costs nothing.
// Scale: wp-fractional-scale + viewporter when available (buffer = logical × scale/120), else the
// integer wl_surface.preferred_buffer_scale / output scale.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <EGL/egl.h>

struct wl_surface;
struct wl_callback;
struct wl_egl_window;
struct zwlr_layer_surface_v1;
struct wp_viewport;
struct wp_fractional_scale_v1;

namespace ks::gfx { class Renderer; }

namespace ks::wl {

class Display;
class Output;

struct RectI { int x, y, w, h; };

class Surface {
public:
    virtual ~Surface() = default;

    // pointer input, in logical surface coordinates
    virtual void pointerMotion(double x, double y) = 0;
    virtual void pointerLeave() = 0;
    virtual void pointerButton(uint32_t button, bool pressed) = 0;
    virtual void pointerScroll(int steps) = 0;

    virtual void drawIfReady() = 0;
};

class LayerSurface : public Surface {
public:
    enum Layer : uint32_t { Background = 0, Bottom = 1, Top = 2, Overlay = 3 };
    enum Anchor : uint32_t { AnchorTop = 1, AnchorBottom = 2, AnchorLeft = 4, AnchorRight = 8 };
    enum Keyboard : uint32_t { KbNone = 0, KbExclusive = 1, KbOnDemand = 2 };

    struct Params {
        Output* output = nullptr;      // null: compositor picks
        Layer layer = Top;
        std::string ns = "kusanagi";
        uint32_t anchor = 0;
        int32_t exclusiveZone = 0;     // -1 = ignore others' zones
        int32_t marginTop = 0, marginRight = 0, marginBottom = 0, marginLeft = 0;
        uint32_t width = 0, height = 0;
        Keyboard keyboard = KbNone;
    };

    LayerSurface(Display& d, Params p);
    ~LayerSurface() override;

    void setExclusiveZone(int32_t zone);
    void setSize(uint32_t w, uint32_t h);
    void setAnchor(uint32_t anchor);
    void setMargins(int32_t top, int32_t right, int32_t bottom, int32_t left);
    void setInputRegion(const std::vector<RectI>& rects);   // empty vector = click-through
    void setInputFull();
    void commit();

    int width() const { return m_w; }
    int height() const { return m_h; }
    double scale() const { return m_scale120 / 120.0; }
    Output* output() const { return m_params.output; }

    void requestRedraw();

    std::function<void(int w, int h)> onResize;
    std::function<bool(double nowMs)> onTick;          // advance animations; true = still running
    std::function<void(gfx::Renderer&)> onRender;
    std::function<void()> onClosed;                    // compositor closed it (output gone)
    std::function<void()> onScaleChanged;

    // pointer: forwarded to whoever owns the widget tree
    std::function<void(double, double)> onPointerMotion;
    std::function<void()> onPointerLeave;
    std::function<void(uint32_t, bool)> onPointerButton;
    std::function<void(int)> onPointerScroll;

    void pointerMotion(double x, double y) override { if (onPointerMotion) onPointerMotion(x, y); }
    void pointerLeave() override { if (onPointerLeave) onPointerLeave(); }
    void pointerButton(uint32_t b, bool p) override { if (onPointerButton) onPointerButton(b, p); }
    void pointerScroll(int s) override { if (onPointerScroll) onPointerScroll(s); }
    void drawIfReady() override;

    // listener plumbing
    void configure(uint32_t serial, uint32_t w, uint32_t h);
    void closed();
    void frameDone();
    void setPreferredScale120(uint32_t s);
    void setIntegerScale(int s);

private:
    void updateBufferSize();
    void draw();

    Display& m_display;
    Params m_params;
    wl_surface* m_surface = nullptr;
    zwlr_layer_surface_v1* m_layer = nullptr;
    wp_viewport* m_viewport = nullptr;
    wp_fractional_scale_v1* m_fractional = nullptr;
    wl_egl_window* m_eglWindow = nullptr;
    EGLSurface m_eglSurface = EGL_NO_SURFACE;
    wl_callback* m_frame = nullptr;
    bool m_configured = false;
    bool m_dirty = false;
    int m_w = 0, m_h = 0;               // logical
    int m_bw = 0, m_bh = 0;             // buffer
    uint32_t m_scale120 = 120;
    int m_intScale = 1;
};

} // namespace ks::wl
