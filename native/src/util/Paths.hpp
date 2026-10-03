// Paths.hpp — XDG locations and small file helpers.
#pragma once
#include <optional>
#include <string>

namespace ks::paths {

std::string home();
std::string configDir();     // ~/.config/kusanagi
std::string stateDir();      // ~/.local/state/kusanagi
std::string cacheDir();      // ~/.cache/kusanagi
std::string runtimeDir();    // $XDG_RUNTIME_DIR/kusanagi
std::string socketPath();    // $XDG_RUNTIME_DIR/kusanagi/<WAYLAND_DISPLAY>.sock
std::string dataDir();       // where assets/ lives (install prefix or the source tree)
std::string expand(const std::string& path);   // leading ~

std::optional<std::string> readFile(const std::string& path);
bool writeFileAtomic(const std::string& path, const std::string& data);
bool mkdirs(const std::string& path);
std::string env(const char* name, const std::string& def = "");

} // namespace ks::paths
