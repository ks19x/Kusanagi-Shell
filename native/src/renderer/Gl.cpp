#include "renderer/Gl.hpp"
#include "app/Log.hpp"

#include <EGL/eglext.h>
#include <wayland-egl.h>

namespace ks::gfx {

Gl& Gl::get() {
    static Gl gl;
    return gl;
}

bool Gl::init(wl_display* display) {
    m_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display, nullptr);
    if (m_display == EGL_NO_DISPLAY || !eglInitialize(m_display, nullptr, nullptr)) {
        log::error("gl", "eglInitialize failed");
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) return false;
    const EGLint cfg[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLint n = 0;
    if (!eglChooseConfig(m_display, cfg, &m_config, 1, &n) || n < 1) {
        log::error("gl", "no RGBA8 GLES3 config");
        return false;
    }
    const EGLint ctx[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, ctx);
    if (m_context == EGL_NO_CONTEXT) {
        log::error("gl", "can't create a GLES 3 context");
        return false;
    }
    // surfaceless current so the renderer can create its GL objects before any window exists
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, m_context);
    log::info("gl", "{} · {}", eglQueryString(m_display, EGL_VENDOR), eglQueryString(m_display, EGL_VERSION));
    return true;
}

void Gl::shutdown() {
    if (m_display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (m_context != EGL_NO_CONTEXT) eglDestroyContext(m_display, m_context);
    eglTerminate(m_display);
    m_display = EGL_NO_DISPLAY;
}

EGLSurface Gl::createSurface(wl_egl_window* win) {
    EGLSurface s = eglCreatePlatformWindowSurface(m_display, m_config, win, nullptr);
    if (s == EGL_NO_SURFACE) log::error("gl", "eglCreatePlatformWindowSurface failed: 0x{:x}", eglGetError());
    return s;
}

void Gl::destroySurface(EGLSurface s) {
    if (m_current == s) {
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, m_context);
        m_current = EGL_NO_SURFACE;
    }
    eglDestroySurface(m_display, s);
}

void Gl::makeCurrent(EGLSurface s) {
    if (m_current == s) return;
    eglMakeCurrent(m_display, s, s, m_context);
    eglSwapInterval(m_display, 0);   // frame callbacks pace us; never block on a hidden output
    m_current = s;
}

void Gl::swap(EGLSurface s) { eglSwapBuffers(m_display, s); }

} // namespace ks::gfx
