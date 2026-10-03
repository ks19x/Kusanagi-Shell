// Gl.hpp — one EGL display + GLES 3 context shared by every surface.
#pragma once
#include <EGL/egl.h>

struct wl_display;
struct wl_egl_window;

namespace ks::gfx {

class Gl {
public:
    static Gl& get();
    bool init(wl_display* display);
    void shutdown();

    EGLSurface createSurface(wl_egl_window* win);
    void destroySurface(EGLSurface s);
    void makeCurrent(EGLSurface s);
    void swap(EGLSurface s);

private:
    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLConfig m_config = nullptr;
    EGLContext m_context = EGL_NO_CONTEXT;
    EGLSurface m_current = EGL_NO_SURFACE;
};

} // namespace ks::gfx
