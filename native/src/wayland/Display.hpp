// Display.hpp — the Wayland connection: registry, globals, outputs (hot-plug), the seat.
// Dirty surfaces are redrawn right before the loop sleeps, at most once per frame callback.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <wayland-client.h>

struct zwlr_layer_shell_v1;
struct xdg_wm_base;
struct wp_viewporter;
struct wp_fractional_scale_manager_v1;
struct wp_cursor_shape_manager_v1;
struct zxdg_output_manager_v1;

namespace ks::wl {

class Output;
class Seat;
class Surface;

class Display {
public:
    Display();
    ~Display();
    static Display& get();

    bool connect();
    void roundtrip() { wl_display_roundtrip(m_display); }
    wl_display* display() const { return m_display; }

    // bind a global some other module needs (ext-workspace, foreign-toplevel, …); null if absent
    void* bind(const wl_interface* iface, uint32_t maxVersion, uint32_t* version = nullptr);
    bool hasGlobal(const char* iface) const { return m_globals.contains(iface); }

    wl_compositor* compositor = nullptr;
    uint32_t compositorVersion = 0;
    zwlr_layer_shell_v1* layerShell = nullptr;
    uint32_t layerShellVersion = 0;
    xdg_wm_base* wmBase = nullptr;
    wp_viewporter* viewporter = nullptr;
    wp_fractional_scale_manager_v1* fractionalScale = nullptr;
    wp_cursor_shape_manager_v1* cursorShape = nullptr;
    zxdg_output_manager_v1* xdgOutput = nullptr;

    const std::vector<std::unique_ptr<Output>>& outputs() const { return m_outputs; }
    Seat* seat() const { return m_seat.get(); }

    std::function<void(Output*)> onOutputAdded;     // after its first `done`
    std::function<void(Output*)> onOutputRemoved;   // before it's destroyed

    void markDirty(Surface* s) { m_dirty.insert(s); }
    void forget(Surface* s) { m_dirty.erase(s); }

    // registry callbacks
    void global(uint32_t name, const char* iface, uint32_t version);
    void globalRemove(uint32_t name);
    void outputReady(Output* o);

private:
    void flushDirty();
    wl_display* m_display = nullptr;
    wl_registry* m_registry = nullptr;
    struct Global { uint32_t name; uint32_t version; };
    std::map<std::string, Global> m_globals;
    std::vector<std::unique_ptr<Output>> m_outputs;
    std::unique_ptr<Seat> m_seat;
    std::set<Surface*> m_dirty;
};

} // namespace ks::wl
