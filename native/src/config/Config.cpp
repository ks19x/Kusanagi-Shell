#include "config/Config.hpp"
#include "app/EventLoop.hpp"
#include "app/Log.hpp"
#include "util/Json.hpp"
#include "util/Paths.hpp"

#include <toml++/toml.hpp>

#include <filesystem>
#include <sstream>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <unistd.h>

namespace ks::cfg {

namespace {
const char* kFollow = "# kusanagi: follow settings.json";

toml::table tableFromJson(const json::Value& v);
toml::array arrayFromJson(const json::Value& v);
std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}
template <class Put> void convert(const json::Value& v, Put put) {
    if (v.isBool()) put(v.boolean());
    else if (v.isNumber()) {
        double d = v.number();
        if (d == double(int64_t(d))) put(int64_t(d));
        else put(d);
    } else if (v.isString()) put(trimmed(v.string()));
    else if (v.isArray()) put(arrayFromJson(v));
    else if (v.isObject()) put(tableFromJson(v));
}
toml::table tableFromJson(const json::Value& v) {
    toml::table t;
    for (auto& [k, val] : v.object()) convert(val, [&](auto&& x) { t.insert(Config::snake(k), std::forward<decltype(x)>(x)); });
    return t;
}
toml::array arrayFromJson(const json::Value& v) {
    toml::array a;
    for (auto& e : v.array()) convert(e, [&](auto&& x) { a.push_back(std::forward<decltype(x)>(x)); });
    return a;
}
} // namespace

struct Config::Impl {
    toml::table table;
    int inotify = -1;
};

Config& Config::get() {
    static Config c;
    if (!c.m_impl) c.m_impl = new Impl;
    return c;
}

std::string Config::path() const { return paths::configDir() + "/config.toml"; }

std::string Config::snake(const std::string& camel) {
    std::string out;
    for (char c : camel) {
        if (c >= 'A' && c <= 'Z') {
            out += '_';
            out += char(c - 'A' + 'a');
        } else out += c;
    }
    return out;
}

void Config::migrate() {
    auto text = paths::readFile(paths::configDir() + "/settings.json");
    if (!text) return;
    std::string err;
    auto v = json::parse(*text, &err);
    if (!v) {
        log::warn("config", "settings.json: {}", err);
        return;
    }
    toml::table t = tableFromJson(*v);
    std::ostringstream ss;
    ss << kFollow << "\n# Converted from settings.json. Delete the line above to make this file the only source.\n\n" << t << "\n";
    paths::mkdirs(paths::configDir());
    paths::writeFileAtomic(path(), ss.str());
    log::info("config", "imported settings.json into config.toml");
}

void Config::load() {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto tomlPath = path(), jsonPath = paths::configDir() + "/settings.json";
    bool haveToml = fs::exists(tomlPath, ec);
    if (!haveToml) migrate();
    else if (fs::exists(jsonPath, ec)) {
        auto first = paths::readFile(tomlPath).value_or("");
        if (first.starts_with(kFollow) && fs::last_write_time(jsonPath, ec) > fs::last_write_time(tomlPath, ec)) migrate();
    }
    reload();
}

void Config::reload() {
    try {
        if (std::filesystem::exists(path())) m_impl->table = toml::parse_file(path());
        else m_impl->table = {};
    } catch (const toml::parse_error& e) {
        log::error("config", "{}: {} (line {})", path(), e.description(), e.source().begin.line);
    }
}

void Config::watch() {
    m_impl->inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    paths::mkdirs(paths::configDir());
    inotify_add_watch(m_impl->inotify, paths::configDir().c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    EventLoop::main().watch(m_impl->inotify, EPOLLIN, [this](uint32_t) {
        char buf[4096];
        bool changed = false;
        ssize_t n;
        while ((n = read(m_impl->inotify, buf, sizeof buf)) > 0)
            for (char* p = buf; p < buf + n;) {
                auto* ev = reinterpret_cast<inotify_event*>(p);
                std::string name = ev->len ? ev->name : "";
                if (name == "config.toml" || name == "settings.json") changed = true;
                p += sizeof(inotify_event) + ev->len;
            }
        if (!changed) return;
        // coalesce bursts (editors write several times)
        static EventLoop::TimerId t = 0;
        EventLoop::main().cancel(t);
        t = EventLoop::main().after(EventLoop::Ms(120), [this] {
            load();
            for (auto& l : m_listeners) l();
        });
    });
}

static const toml::node* lookup(const toml::table& t, const std::string& path) {
    return t.at_path(path).node();
}

double Config::num(const std::string& p, double def) const {
    auto* n = lookup(m_impl->table, p);
    if (!n) return def;
    if (auto v = n->value<double>()) return *v;
    return def;
}

bool Config::flag(const std::string& p, bool def) const {
    auto* n = lookup(m_impl->table, p);
    if (!n) return def;
    return n->value<bool>().value_or(def);
}

std::string Config::str(const std::string& p, const std::string& def) const {
    auto* n = lookup(m_impl->table, p);
    if (!n) return def;
    return n->value<std::string>().value_or(def);
}

std::vector<std::string> Config::list(const std::string& p) const {
    std::vector<std::string> out;
    auto* n = lookup(m_impl->table, p);
    if (n && n->is_array())
        for (auto& e : *n->as_array())
            if (auto s = e.value<std::string>()) out.push_back(*s);
    return out;
}

bool Config::set(const std::string& p, const std::string& tomlValue) {
    toml::table parsed;
    try {
        parsed = toml::parse("v = " + tomlValue);
    } catch (...) {
        return false;
    }
    auto dot = p.rfind('.');
    toml::table* tbl = &m_impl->table;
    if (dot != std::string::npos) {
        std::string parent = p.substr(0, dot);
        std::istringstream ss(parent);
        std::string part;
        while (std::getline(ss, part, '.')) {
            auto* next = (*tbl)[part].as_table();
            if (!next) {
                tbl->insert_or_assign(part, toml::table{});
                next = (*tbl)[part].as_table();
            }
            tbl = next;
        }
    }
    std::string key = dot == std::string::npos ? p : p.substr(dot + 1);
    tbl->insert_or_assign(key, *parsed.get("v"));
    std::ostringstream ss;
    auto first = paths::readFile(path()).value_or("");
    ss << "# Kusanagi settings — edited live by Kusanagi; comments here are not kept.\n\n" << m_impl->table << "\n";
    (void)first;
    return paths::writeFileAtomic(path(), ss.str());
}

} // namespace ks::cfg
