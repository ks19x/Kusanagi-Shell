// Theme.hpp — the colours: ~/.config/kusanagi/colors.json (written by the palette generator on
// every wallpaper change; watched) unless look.palette picks a built-in palette; look.accent pins.
#pragma once
#include "renderer/Renderer.hpp"

#include <functional>
#include <string>
#include <vector>

namespace ks {

struct Theme {
    gfx::Color text, textDim, danger, accent, accent2, border, bgPanel, bgCard, borderAccent, textFaint, ok, trackBg;
    std::string font;

    gfx::Color bg() const { return bgPanel.alpha(0.7f); }
    gfx::Color surfaceBorder() const;
    float surfaceBorderWidth() const;

    static Theme& get();
    void load();
    void watch();
    void onChanged(std::function<void()> fn) { m_listeners.push_back(std::move(fn)); }
    void notify();

private:
    std::vector<std::function<void()>> m_listeners;
    int m_inotify = -1;
};

inline Theme& theme() { return Theme::get(); }

} // namespace ks
