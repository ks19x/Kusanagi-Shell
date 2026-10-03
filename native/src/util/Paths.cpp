#include "util/Paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ks::paths {

std::string env(const char* name, const std::string& def) {
    const char* v = getenv(name);
    return v && *v ? v : def;
}

std::string home() { return env("HOME", "/tmp"); }
std::string configDir() { return env("XDG_CONFIG_HOME", home() + "/.config") + "/kusanagi"; }
std::string stateDir() { return env("XDG_STATE_HOME", home() + "/.local/state") + "/kusanagi"; }
std::string cacheDir() { return env("XDG_CACHE_HOME", home() + "/.cache") + "/kusanagi"; }
std::string runtimeDir() { return env("XDG_RUNTIME_DIR", "/tmp") + "/kusanagi"; }
std::string socketPath() { return runtimeDir() + "/" + env("WAYLAND_DISPLAY", "wayland-0") + ".sock"; }

std::string dataDir() {
    // running from the build tree: <repo>/native/build/kusanagi -> <repo>
    std::error_code ec;
    auto exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        for (auto dir = exe.parent_path(); !dir.empty() && dir != dir.root_path(); dir = dir.parent_path())
            if (fs::exists(dir / "assets" / "logo.svg")) return dir.string();
        auto share = exe.parent_path().parent_path() / "share" / "kusanagi";
        if (fs::exists(share)) return share.string();
    }
    return "/usr/share/kusanagi";
}

std::string expand(const std::string& path) {
    if (path == "~") return home();
    if (path.starts_with("~/")) return home() + path.substr(1);
    return path;
}

std::optional<std::string> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
    auto tmp = path + ".tmp-" + std::to_string(getpid());
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << data;
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    return !ec;
}

bool mkdirs(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

} // namespace ks::paths
