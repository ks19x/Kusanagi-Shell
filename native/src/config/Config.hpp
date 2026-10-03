// Config.hpp — ~/.config/kusanagi/config.toml, read by dotted path ("bar.height").
// Same keys as v4's settings.json, snake_cased (bar.fontSize → bar.font_size). First run converts
// settings.json. While the file still carries the "follow settings.json" header line, changes to
// settings.json (v4's Settings app) are re-imported, so both shells stay in sync during the move.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace ks::cfg {

class Config {
public:
    static Config& get();
    void load();
    void watch();                         // inotify on the config dir

    double num(const std::string& path, double def) const;
    int integer(const std::string& path, int def) const { return int(num(path, def)); }
    bool flag(const std::string& path, bool def) const;
    std::string str(const std::string& path, const std::string& def) const;
    std::vector<std::string> list(const std::string& path) const;

    // write one value back (format-preserving where possible); used by IPC / settings
    bool set(const std::string& path, const std::string& tomlValue);

    void onChanged(std::function<void()> fn) { m_listeners.push_back(std::move(fn)); }
    std::string path() const;
    static std::string snake(const std::string& camel);

private:
    void migrate();
    void reload();
    struct Impl;
    Impl* m_impl = nullptr;
    std::vector<std::function<void()>> m_listeners;
};

inline Config& config() { return Config::get(); }

} // namespace ks::cfg
