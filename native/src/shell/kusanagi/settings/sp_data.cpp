// Reading and writing settings.json for the Settings window: the defaults, reset, and a short save delay
// so a slider drag ends up as one write.

#include "core/deferred_call.h"
#include "core/log.h"
#include "core/process/process.h"
#include "core/timer_manager.h"
#include "shell/control_center/control_center_services.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "util/file_utils.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace kusanagi::sp {

  namespace {

    constexpr Logger kLog("kusanagi-settings");

    // Default settings. Keep in sync with what a fresh install should write.
    constexpr const char* kDefaults = R"JSON({
      "look": { "font": "JetBrainsMono Nerd Font", "radius": 16, "accent": "", "palette": "wallpaper", "animSpeed": 1.0,
                "shadows": true, "preload": true, "bounce": 1.0, "backdrop": 0.25, "borders": true, "borderAccent": false },
      "bar": { "style": "islands", "position": "top", "height": 28, "layout": "classic", "accentLabels": false,
               "opacity": 0.5, "radius": 10, "fontSize": 11, "outline": false, "clock": "HH:mm", "clockBold": true, "trayIconSize": 14,
               "hoverGrow": true, "scrollClock": "volume", "scrollStats": "volume", "volumeStep": 5,
               "mediaPopup": true, "marquee": true, "mediaWidth": 20, "titleWidth": 60,
               "modules": { "title": false, "media": true, "cpu": true, "ram": true, "gpu": false, "temp": false,
                            "volume": true, "network": true, "tray": true, "power": true } },
      "workspaces": { "style": "pills", "shown": 5, "glow": true, "icons": "", "activeColor": "accent" },
      "panel": { "opacity": 0.95, "width": 560, "defaultTab": 0, "showMedia": true, "showStats": true, "showWeather": true, "morph": "island",
                 "tileStyle": "cards", "tileColumns": 4, "sliderStyle": "thick", "header": "big", "tabs": true,
                 "order": ["tiles", "sliders", "media", "weather", "stats"],
                 "tiles": ["nightlight", "dnd", "mic", "gamemode", "screenshot", "record", "colorpicker", "wallpaper"] },
      "osd": { "position": "top", "timeout": 1400, "volume": true, "mic": true, "gamemode": true, "brightness": true, "style": "pill", "showValue": true },
      "notifications": { "position": "top-right", "timeout": 5000, "max": 5, "style": "comfortable", "progress": true, "images": true },
      "launcher": { "style": "card", "position": "upper", "width": 640, "rows": 7, "descriptions": true, "sortByUsage": true, "terminal": "foot",
                    "layout": "list", "iconSize": 32, "commands": true, "webSearch": true, "searchEngine": "https://duckduckgo.com/?q=%s" },
      "wallpaper": { "folder": "~/Pictures/Wallpapers", "columns": 4, "renderer": "kusanagi", "transition": "random", "duration": 1100,
                     "fill": "fill", "parallax": 0.04, "dim": 0, "slideshow": 0 },
      "lock": { "engine": "hyprlock", "style": "center", "blur": 0.8, "dim": 0.35, "clock": "HH:mm", "avatar": true, "media": true, "greeting": "" },
      "power": { "style": "row" },
      "greeter": { "style": "", "user": "", "session": "" },
      "display": { "nightTemp": 4000 },
      "windows": { "override": false, "gapsIn": 8, "gapsOut": 8, "border": 2 },
      "gamemode": { "auto": true, "effects": true, "feral": true, "quiet": true, "dnd": true, "grace": 800, "announce": "manual" },
      "screenshot": { "position": "bottom-right", "timeout": 6000, "editor": "swappy -f" },
      "weather": { "location": "", "units": "metric" },
      "dock": { "pinned": [], "indicator": "dot", "magnify": 1.35, "grouped": true },
      "idle": { "enabled": false, "lock": 10, "screenOff": 15, "suspend": 0, "media": true, "notify": true },
      "polkit": { "enabled": true },
      "recorder": { "folder": "~/Videos", "fps": 60, "quality": "very_high", "replay": 30, "audio": "desktop", "capture": "screen",
                    "codec": "auto", "streamUrl": "" },
      "updates": { "interval": 3, "notify": false },
      "brightness": { "step": 5, "ddc": true },
      "bluetooth": { "enabled": true, "autoScan": true }
    })JSON";

    std::vector<std::string> splitPath(std::string_view path) {
      std::vector<std::string> out;
      std::size_t start = 0;
      while (start <= path.size()) {
        const auto dot = path.find('.', start);
        out.emplace_back(path.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
        if (dot == std::string_view::npos) break;
        start = dot + 1;
      }
      return out;
    }

    const json* find(const json& root, const std::vector<std::string>& parts) {
      const json* cur = &root;
      for (const auto& p : parts) {
        if (!cur->is_object()) return nullptr;
        const auto it = cur->find(p);
        if (it == cur->end()) return nullptr;
        cur = &*it;
      }
      return cur;
    }

    // Write whole-number reals as integers ("animSpeed": 1) so existing files don't churn.
    json normalized(const json& v) {
      if (v.is_number_float()) {
        const double d = v.get<double>();
        if (std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 1e15) return json(static_cast<std::int64_t>(d));
      }
      return v;
    }

    void apply(json& root, const std::string& path, const json& v) {
      const auto parts = splitPath(path);
      if (parts.empty()) return;
      json* cur = &root;
      for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        json& next = (*cur)[parts[i]];
        if (!next.is_object()) next = json::object();
        cur = &next;
      }
      if (v.is_null()) {
        cur->erase(parts.back());
      } else {
        (*cur)[parts.back()] = normalized(v);
      }
    }

    struct State {
      Host host;
      std::vector<std::pair<const void*, Host>> extraHosts; // From addHost(), e.g. the setup window.
      ControlCenterServices services;
      std::vector<std::pair<std::string, json>> pending; // Applied in memory, not yet saved to the file.
      Timer* saveTimer = new Timer(); // Leaked on purpose to avoid static destruction order issues.
      bool changedQueued = false;
    };
    State& state() {
      static State s;
      return s;
    }

    std::filesystem::path settingsPath() {
      return std::filesystem::path(FileUtils::configDir()) / "settings.json";
    }

    void notifyChanged() {
      State& s = state();
      if (s.changedQueued) return;
      s.changedQueued = true;
      DeferredCall::callLater([]() {
        state().changedQueued = false;
        if (state().host.changed) state().host.changed();
        const auto extra = state().extraHosts; // Copy: a host may remove itself while it syncs.
        for (const auto& [owner, h] : extra) {
          if (h.changed) h.changed();
        }
      });
    }

  } // namespace

  const json& defaults() {
    static const json d = json::parse(kDefaults);
    return d;
  }

  json value(std::string_view path) {
    const auto parts = splitPath(path);
    if (const json* v = find(kusanagi::settings(), parts); v != nullptr && !v->is_null()) return *v;
    if (const json* v = find(defaults(), parts); v != nullptr) return *v;
    return nullptr;
  }

  bool truthy(const json& v) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() != 0.0;
    if (v.is_string()) return !v.get<std::string>().empty();
    return !v.is_null();
  }

  bool write(std::vector<std::pair<std::string, json>> changes) {
    if (changes.empty()) return true;
    State& s = state();
    json mem = kusanagi::settings();
    for (auto& [path, v] : changes) {
      apply(mem, path, v);
      s.pending.emplace_back(std::move(path), std::move(v));
    }
    kusanagi::setSettings(std::move(mem));
    s.saveTimer->start(std::chrono::milliseconds(250), []() { flush(); });
    notifyChanged();
    return true;
  }

  void flush() {
    State& s = state();
    s.saveTimer->stop();
    if (s.pending.empty()) return;
    const auto path = settingsPath();
    json file = json::object();
    {
      std::ifstream in(path);
      if (in) {
        file = json::parse(in, nullptr, false);
        if (!file.is_object()) {
          // Never clobber a file we couldn't parse (maybe a half-written hand edit); try again later.
          kLog.warn("settings.json isn't valid JSON; not writing");
          s.saveTimer->start(std::chrono::milliseconds(1000), []() { flush(); });
          return;
        }
      }
    }
    for (const auto& [p, v] : s.pending) apply(file, p, v);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = path.string() + ".tmp";
    {
      std::ofstream out(tmp);
      if (!out) {
        kLog.warn("couldn't write {}", tmp);
        return;
      }
      out << file.dump(4) << "\n";
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
      kLog.warn("couldn't replace settings.json: {}", ec.message());
      return;
    }
    s.pending.clear();
    kusanagi::setSettings(std::move(file));
  }

  // A reload read the file while writes were still pending, so put them back on top.
  void reapplyPending() {
    State& s = state();
    if (s.pending.empty()) return;
    json mem = kusanagi::settings();
    for (const auto& [p, v] : s.pending) apply(mem, p, v);
    kusanagi::setSettings(std::move(mem));
  }

  bool reset(std::string_view section) {
    std::vector<std::pair<std::string, json>> changes;
    for (const auto& [name, d] : defaults().items()) {
      if (!section.empty() && name != section) continue;
      // Nested objects are reset member by member, everything else as a whole.
      for (const auto& [k, v] : d.items()) {
        if (v.is_object()) {
          for (const auto& [m, mv] : v.items()) changes.emplace_back(name + "." + k + "." + m, mv);
        } else {
          changes.emplace_back(name + "." + k, v);
        }
      }
    }
    return write(std::move(changes));
  }

  void refresh() { notifyChanged(); }

  void setHost(Host host) { state().host = std::move(host); }

  void addHost(const void* owner, Host host) {
    removeHost(owner);
    state().extraHosts.emplace_back(owner, std::move(host));
  }

  void removeHost(const void* owner) {
    std::erase_if(state().extraHosts, [owner](const auto& e) { return e.first == owner; });
  }

  void setServices(const ControlCenterServices& services) { state().services = services; }

  const ControlCenterServices& services() { return state().services; }

  void requestLayout() {
    if (state().host.requestLayout) state().host.requestLayout();
    for (const auto& [owner, h] : state().extraHosts) {
      if (h.requestLayout) h.requestLayout();
    }
  }

  std::string ipc(const std::string& line) {
    if (state().host.ipc) return state().host.ipc(line);
    for (const auto& [owner, h] : state().extraHosts) {
      if (h.ipc) return h.ipc(line);
    }
    return "error: no shell\n";
  }

  void spawn(std::vector<std::string> argv) {
    if (argv.empty()) return;
    (void)process::runAsync(argv);
  }

  void run(std::vector<std::string> argv, std::function<void(const std::string& out, int code)> done) {
    if (argv.empty()) return;
    auto cb = std::make_shared<std::function<void(const std::string&, int)>>(std::move(done));
    const bool started = process::runAsync(argv, process::RunCallbacks{
                                                     .onExit = [cb](process::RunResult r) {
                                                       DeferredCall::callLater([cb, out = std::move(r.out), code = r.exitCode]() {
                                                         if (*cb) (*cb)(out, code);
                                                       });
                                                     },
                                                 });
    if (!started && *cb) (*cb)("", -1);
  }

  std::string expandHome(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    return std::string(home != nullptr ? home : "") + path.substr(1);
  }

  Binding bind(std::string path, std::function<void(const json&)> after) {
    return Binding{
        .get = [path]() { return value(path); },
        .set = [path, after = std::move(after)](const json& v) {
          set(path, v);
          if (after) after(v);
        },
    };
  }

} // namespace kusanagi::sp
