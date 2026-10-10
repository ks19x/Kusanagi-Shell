#include "shell/kusanagi/wm_layout.h"

#include "compositors/compositor_detect.h"
#include "core/deferred_call.h"
#include "core/log.h"
#include "core/process/process.h"
#include "core/timer_manager.h"
#include "shell/kusanagi/kusanagi_style.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace kusanagi::wm {

  namespace {
    constexpr Logger kLog("kusanagi-wm");

    std::string env(const char* k) {
      const char* v = std::getenv(k);
      return v != nullptr ? v : "";
    }
    std::string home() { return env("HOME"); }
    std::string configHome() {
      const std::string x = env("XDG_CONFIG_HOME");
      return x.empty() ? home() + "/.config" : x;
    }
    // Hyprland 0.55+ with a Lua config takes Lua, a classic hyprland.conf the old keywords.
    bool hyprLua() {
      std::error_code ec;
      return kind() == Kind::Hyprland && std::filesystem::exists(configHome() + "/hypr/hyprland.lua", ec);
    }

    struct Windows {
      bool override = false;
      int gapsIn = 8;
      int gapsOut = 8;
      int border = 2;
      bool operator==(const Windows&) const = default;
    };
    Windows current() {
      return Windows{
          .override = opt<bool>("windows", "override", false),
          .gapsIn = opt<int>("windows", "gapsIn", 8),
          .gapsOut = opt<int>("windows", "gapsOut", 8),
          .border = opt<int>("windows", "border", 2),
      };
    }

    struct State {
      Layout layout;          // the compositor config's values
      std::string swayBorder = "normal"; // sway's default_border style (normal | pixel | none): restored as it was
      bool started = false;
      bool layoutApplied = false;
      std::optional<Windows> last; // what was applied last
      Timer relayout;
    };
    State& state() {
      static auto* s = new State(); // never destroyed: its Timer mustn't outlive the TimerManager
      return *s;
    }

    void run(const std::vector<std::string>& argv) { (void)process::runAsync(argv); }

    // The config's own gappih, gappoh and borderpx (config.conf, then rice.conf).
    void readMangoLayout() {
      static const std::regex line("^(gappih|gappoh|borderpx)=(\\d+)");
      Layout& l = state().layout;
      for (const char* file : {"/mango/config.conf", "/mango/rice.conf"}) {
        std::ifstream in(configHome() + file);
        for (std::string s; std::getline(in, s);) {
          std::smatch m;
          if (!std::regex_search(s, m, line)) continue;
          const int v = std::stoi(m[2].str());
          if (m[1] == "gappih") l.gapsIn = v;
          else if (m[1] == "gappoh") l.gapsOut = v;
          else l.border = v;
        }
      }
    }

    // sway: gaps inner / gaps outer / default_border from the files in ~/.config/sway. sway adds the inner gap at
    // the screen edges too, so the edge gap Kusanagi shows is outer + inner. sway's defaults: 0, 0, normal 2.
    void readSwayLayout() {
      static const std::regex gaps("^\\s*gaps\\s+(inner|outer)\\s+(-?\\d+)\\s*$");
      static const std::regex border("^\\s*default_border\\s+(normal|pixel|none)(?:\\s+(\\d+))?");
      State& st = state();
      int inner = 0;
      int outer = 0;
      int px = 2;
      std::error_code ec;
      for (const auto& e : std::filesystem::directory_iterator(configHome() + "/sway", ec)) {
        const std::string name = e.path().filename().string();
        if (!e.is_regular_file(ec) || name.find(".bak") != std::string::npos || name.starts_with("kusanagi.")) continue;
        std::ifstream in(e.path());
        for (std::string l; std::getline(in, l);) {
          std::smatch m;
          if (std::regex_search(l, m, gaps)) {
            (m[1] == "inner" ? inner : outer) = std::stoi(m[2].str());
          } else if (std::regex_search(l, m, border)) {
            st.swayBorder = m[1].str();
            if (m[2].matched) px = std::stoi(m[2].str());
          }
        }
      }
      st.layout = Layout{std::max(0, inner), std::max(0, outer + inner), px};
    }

    // Ask Hyprland before anything is overridden. A gaps option prints "css": "4 4 4 4".
    void readHyprLayout() {
      (void)process::runAsync(
          std::vector<std::string>{"sh", "-c", "for o in gaps_in gaps_out border_size; do hyprctl -j getoption general:$o | tr -d '\\n'; echo; done"},
          process::RunCallbacks{.onExit = [](process::RunResult r) {
            DeferredCall::callLater([out = std::move(r.out)]() {
              static const std::regex num("\"(?:css|custom|int)\"\\s*:\\s*\"?(\\d+)");
              std::vector<int> n;
              std::istringstream in(out);
              for (std::string l; std::getline(in, l);) {
                std::smatch m;
                n.push_back(std::regex_search(l, m, num) ? std::stoi(m[1].str()) : -1);
              }
              if (n.size() >= 3 && n[0] >= 0 && n[1] >= 0 && n[2] >= 0) state().layout = Layout{n[0] * 2, n[1], n[2]};
            });
          }}
      );
    }

    void setLayout(int gapsIn, int gapsOut, int border, const std::string& borderStyle = "") {
      const std::string i = std::to_string(std::max(0, gapsIn));
      const std::string o = std::to_string(std::max(0, gapsOut));
      const std::string b = std::to_string(std::max(0, border));
      if (kind() == Kind::Mango) {
        run({"sh", "-c",
             "mmsg dispatch setoption,gappih," + i + "; mmsg dispatch setoption,gappiv," + i + "; mmsg dispatch setoption,gappoh," + o
                 + "; mmsg dispatch setoption,gappov," + o + "; mmsg dispatch setoption,borderpx," + b});
      } else if (kind() == Kind::Sway) {
        // Tiled windows only: floating ones keep their own borders (sway's default_floating_border).
        const std::string style = borderStyle.empty() ? "pixel" : borderStyle;
        const std::string bs = style == "none" ? "none" : style + " " + b;
        run({"swaymsg", "gaps inner all set " + i + "; gaps outer all set " + std::to_string(std::max(0, gapsOut) - std::max(0, gapsIn))
                            + "; default_border " + bs + "; [tiling] border " + bs});
      } else if (kind() == Kind::Hyprland) {
        const std::string half = std::to_string(static_cast<int>(std::lround(std::max(0, gapsIn) / 2.0)));
        if (hyprLua()) {
          run({"hyprctl", "eval", "hl.config({ general = { gaps_in = " + half + ", gaps_out = " + o + ", border_size = " + b + " } })"});
        } else {
          run({"hyprctl", "--batch", "keyword general:gaps_in " + half + "; keyword general:gaps_out " + o + "; keyword general:border_size " + b});
        }
      }
    }

    // ~/.config/kusanagi/mango.conf holds the override while it's on and is empty while it's off. It's never
    // created just to be empty.
    void writeMangoFile(const Windows& w) {
      const std::filesystem::path path = home() + "/.config/kusanagi/mango.conf";
      std::string want;
      if (w.override) {
        want = "# written by Kusanagi (Settings → Display → Windows) — turn the override off there to drop it\n"
               "gappih=" + std::to_string(w.gapsIn) + "\ngappiv=" + std::to_string(w.gapsIn) + "\ngappoh=" + std::to_string(w.gapsOut)
               + "\ngappov=" + std::to_string(w.gapsOut) + "\nborderpx=" + std::to_string(w.border) + "\n";
      }
      std::string have;
      {
        std::ifstream in(path);
        if (in) {
          std::stringstream ss;
          ss << in.rdbuf();
          have = ss.str();
        }
      }
      if (have == want) return;
      std::error_code ec;
      std::filesystem::create_directories(path.parent_path(), ec);
      std::ofstream out(path, std::ios::trunc);
      if (!out) {
        kLog.warn("couldn't write {}", path.string());
        return;
      }
      out << want;
    }

    void applyLayout() {
      State& s = state();
      const Windows w = current();
      s.last = w;
      if (kind() == Kind::Mango) writeMangoFile(w);
      if (w.override) {
        setLayout(w.gapsIn, w.gapsOut, w.border);
        s.layoutApplied = true;
      } else if (s.layoutApplied) {
        s.layoutApplied = false;
        if (kind() == Kind::Hyprland) run({"hyprctl", "reload"}); // back to whatever the config says
        else setLayout(s.layout.gapsIn, s.layout.gapsOut, s.layout.border, kind() == Kind::Sway ? s.swayBorder : "");
      }
    }
  } // namespace

  Kind kind() {
    switch (compositors::detect()) {
    case compositors::CompositorKind::Mango: return Kind::Mango;
    case compositors::CompositorKind::Hyprland: return Kind::Hyprland;
    case compositors::CompositorKind::Niri: return Kind::Niri;
    case compositors::CompositorKind::Sway: return Kind::Sway;
    case compositors::CompositorKind::Labwc: return Kind::Labwc;
    case compositors::CompositorKind::Kde: return Kind::Kde;
    case compositors::CompositorKind::Dwl: return Kind::Dwl;
    default: return Kind::Other;
    }
  }

  std::string name() {
    switch (kind()) {
    case Kind::Mango: return "MangoWM";
    case Kind::Hyprland: return "Hyprland";
    case Kind::Niri: return "niri";
    case Kind::Sway: return "sway";
    case Kind::Labwc: return "labwc";
    case Kind::Kde: return "KDE Plasma";
    case Kind::Dwl: return "dwl";
    default: return "Wayland";
    }
  }

  std::string configFile() {
    switch (kind()) {
    case Kind::Mango: return configHome() + "/mango/config.conf";
    case Kind::Hyprland: return configHome() + (hyprLua() ? "/hypr/hyprland.lua" : "/hypr/hyprland.conf");
    case Kind::Niri: return configHome() + "/niri/config.kdl";
    case Kind::Sway: return configHome() + "/sway/config";
    case Kind::Labwc: return configHome() + "/labwc/rc.xml";
    default: return ""; // KDE: System Settings; dwl: its config.h lives wherever it was built
    }
  }

  std::vector<std::string> monitorsCommand() {
    switch (kind()) {
    case Kind::Mango: return {"mmsg", "get", "all-monitors"};
    case Kind::Hyprland: return {"hyprctl", "monitors", "-j"};
    case Kind::Niri: return {"niri", "msg", "-j", "outputs"};
    case Kind::Sway: return {"swaymsg", "-r", "-t", "get_outputs"};
    default: return {};
    }
  }

  std::vector<Monitor> parseMonitors(const std::string& text) {
    using json = nlohmann::json;
    std::vector<Monitor> out;
    const json d = json::parse(text, nullptr, false);
    if (d.is_discarded()) return out;
    const auto num = [](const json& o, const char* k, double fallback) {
      const auto it = o.find(k);
      return it != o.end() && it->is_number() ? it->get<double>() : fallback;
    };
    try {
      if (kind() == Kind::Mango && d.is_object() && d.contains("monitors") && d["monitors"].is_array()) {
        for (const auto& m : d["monitors"]) {
          out.push_back(Monitor{m.value("name", ""), static_cast<int>(num(m, "width", 0)), static_cast<int>(num(m, "height", 0)), 0.0,
                                num(m, "scale", 1.0), static_cast<int>(num(m, "x", 0)), static_cast<int>(num(m, "y", 0))});
        }
      } else if (kind() == Kind::Hyprland && d.is_array()) {
        for (const auto& m : d) {
          out.push_back(Monitor{m.value("name", ""), static_cast<int>(num(m, "width", 0)), static_cast<int>(num(m, "height", 0)),
                                num(m, "refreshRate", 0.0), num(m, "scale", 1.0), static_cast<int>(num(m, "x", 0)),
                                static_cast<int>(num(m, "y", 0))});
        }
      } else if (kind() == Kind::Sway && d.is_array()) {
        for (const auto& m : d) {
          if (!m.value("active", true)) continue;
          const json rect = m.value("rect", json::object());
          const json mode = m.value("current_mode", json::object());
          out.push_back(Monitor{m.value("name", ""), static_cast<int>(num(mode, "width", num(rect, "width", 0))),
                                static_cast<int>(num(mode, "height", num(rect, "height", 0))), num(mode, "refresh", 0.0) / 1000.0,
                                num(m, "scale", 1.0), static_cast<int>(num(rect, "x", 0)), static_cast<int>(num(rect, "y", 0))});
        }
      } else if (kind() == Kind::Niri && d.is_object()) {
        for (const auto& [k, o] : d.items()) {
          const json lg = o.value("logical", json::object());
          const json* mode = nullptr;
          if (o.contains("modes") && o["modes"].is_array() && o.contains("current_mode") && o["current_mode"].is_number()) {
            const auto i = o["current_mode"].get<std::size_t>();
            if (i < o["modes"].size()) mode = &o["modes"][i];
          }
          out.push_back(Monitor{k, static_cast<int>(mode != nullptr ? num(*mode, "width", 0) : num(lg, "width", 0)),
                                static_cast<int>(mode != nullptr ? num(*mode, "height", 0) : num(lg, "height", 0)),
                                mode != nullptr ? num(*mode, "refresh_rate", 0) / 1000.0 : 0.0, num(lg, "scale", 1.0),
                                static_cast<int>(num(lg, "x", 0)), static_cast<int>(num(lg, "y", 0))});
        }
      }
    } catch (...) {
      out.clear();
    }
    return out;
  }

  bool canSetLayout() { return kind() == Kind::Mango || kind() == Kind::Hyprland || kind() == Kind::Sway; }

  Layout layout() { return state().layout; }

  void start() {
    State& s = state();
    if (s.started || !canSetLayout()) return;
    s.started = true;
    if (kind() == Kind::Mango) readMangoLayout();
    else if (kind() == Kind::Sway) readSwayLayout();
    else if (!current().override) readHyprLayout();
    s.relayout.start(std::chrono::milliseconds(30), []() { applyLayout(); });
  }

  void settingsChanged() {
    State& s = state();
    if (!s.started) return;
    // Only when windows.* changed. A reload of anything else leaves the compositor alone.
    if (s.last && *s.last == current()) return;
    s.relayout.start(std::chrono::milliseconds(30), []() { applyLayout(); });
  }

} // namespace kusanagi::wm
