// Output.hpp — one monitor: name (DP-1…), logical geometry (xdg-output), integer scale.
#pragma once
#include <cstdint>
#include <functional>
#include <algorithm>
#include <string>

struct wl_output;
struct zxdg_output_v1;
struct zxdg_output_manager_v1;

namespace ks::wl {

class Display;

class Output {
public:
    Output(Display* d, wl_output* o, uint32_t globalName);
    ~Output();

    wl_output* handle() const { return m_output; }
    uint32_t globalName() const { return m_global; }
    bool ready() const { return m_announced; }
    const std::string& name() const { return m_name; }
    const std::string& description() const { return m_desc; }
    int x() const { return m_lx; }
    int y() const { return m_ly; }
    int width() const { return m_lw > 0 ? m_lw : m_modeW / std::max(1, m_scale); }
    int height() const { return m_lh > 0 ? m_lh : m_modeH / std::max(1, m_scale); }
    int scale() const { return m_scale; }

    std::function<void()> onChanged;   // geometry / scale changed after it was announced

    void attachXdgOutput(zxdg_output_manager_v1* mgr);

    // listener plumbing
    void setMode(int w, int h) { m_modeW = w; m_modeH = h; }
    void setScale(int s) { m_scale = s; }
    void setName(const char* n) { m_name = n; }
    void setDescription(const char* d) { m_desc = d; }
    void setLogicalPos(int x, int y) { m_lx = x; m_ly = y; }
    void setLogicalSize(int w, int h) { m_lw = w; m_lh = h; }
    void done();

private:
    Display* m_display;
    wl_output* m_output;
    zxdg_output_v1* m_xdg = nullptr;
    uint32_t m_global;
    bool m_announced = false;
    std::string m_name, m_desc;
    int m_modeW = 0, m_modeH = 0, m_scale = 1;
    int m_lx = 0, m_ly = 0, m_lw = 0, m_lh = 0;
};

} // namespace ks::wl
