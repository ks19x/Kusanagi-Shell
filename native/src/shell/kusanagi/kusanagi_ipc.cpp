#include "shell/kusanagi/kusanagi_ipc.h"

#include "cli/schema_msg.h"
#include "core/deferred_call.h"
#include "core/log.h"
#include "core/process/process.h"
#include "core/timer_manager.h"
#include "dbus/bluetooth/bluetooth_service.h"
#include "dbus/mpris/mpris_service.h"
#include "idle/idle_inhibitor.h"
#include "ipc/ipc_service.h"
#include "shell/kusanagi/bar_templates.h"
#include "shell/kusanagi/game_mode.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/presets.h"
#include "shell/launcher/launcher_panel.h"
#include "system/brightness_service.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace kusanagi {

  namespace {
    constexpr Logger kLog("kusanagi-ipc");

    std::string env(const char* name) {
      const char* v = std::getenv(name);
      return v != nullptr ? std::string(v) : std::string();
    }

    // Splits "<action> <rest>" into the first word and everything after it, as typed.
    std::pair<std::string, std::string> split(const std::string& args) {
      const auto b = args.find_first_not_of(' ');
      if (b == std::string::npos) return {};
      const auto space = args.find(' ', b);
      if (space == std::string::npos) return {args.substr(b), {}};
      const auto r = args.find_first_not_of(' ', space);
      return {args.substr(b, space - b), r == std::string::npos ? std::string() : args.substr(r)};
    }
  } // namespace

  std::string cliCommand() {
    const std::string c = env("KUSANAGI_CLI");
    return c.empty() ? "kusanagi" : c;
  }

  namespace recorder {
    std::string mode() {
      const std::string rt = env("XDG_RUNTIME_DIR");
      std::ifstream f((rt.empty() ? std::string("/tmp") : rt) + "/kusanagi/record.json");
      if (!f) return "off";
      const auto j = nlohmann::json::parse(f, nullptr, false);
      if (!j.is_object() || !j.contains("mode") || !j["mode"].is_string() || j["mode"].get<std::string>().empty()) {
        return "off";
      }
      return j["mode"].get<std::string>();
    }

    void run(std::string_view what) {
      // The recorder settings go to `kusanagi record` as KR_* variables.
      std::string folder = opt<std::string>("recorder", "folder", "~/Videos");
      if (folder.starts_with("~")) folder = env("HOME") + folder.substr(1);
      std::vector<std::string> cmd{
          "env",
          "KR_FOLDER=" + folder,
          "KR_FPS=" + std::to_string(opt<int>("recorder", "fps", 60)),
          "KR_QUALITY=" + opt<std::string>("recorder", "quality", "very_high"),
          "KR_REPLAY=" + std::to_string(opt<int>("recorder", "replay", 30)),
          "KR_AUDIO=" + opt<std::string>("recorder", "audio", "desktop"),
          "KR_CAPTURE=" + opt<std::string>("recorder", "capture", "screen"),
          "KR_CODEC=" + opt<std::string>("recorder", "codec", "auto"),
          "KR_STREAM_URL=" + opt<std::string>("recorder", "streamUrl", ""),
          cliCommand(),
          "record",
          std::string(what),
      };
      if (!process::runAsync(cmd)) kLog.warn("couldn't run {} record {}", cliCommand(), what);
    }

    void smart() {
      const std::string m = mode();
      if (m == "replay") run("save");
      else if (m != "off") run("stop");
      else run("record");
    }
  } // namespace recorder

  namespace updates {
    namespace {
      struct State {
        int count = 0;
        bool known = false; // false until a check worked
        bool checking = false;
        long long checkedAt = 0;
        std::vector<std::pair<std::string, std::string>> list; // { name, source }, the lines after the count
        Timer after;        // re-counts once the upgrade terminal has had time to finish
      };
      State& state() {
        static auto* s = new State(); // never destroyed: its Timer mustn't outlive the TimerManager at exit
        return *s;
      }

      void finished(const std::string& out) {
        State& s = state();
        s.checking = false;
        std::istringstream in(out);
        std::string line;
        while (std::getline(in, line) && line.find_first_not_of(" \t\r") == std::string::npos) {
        }
        int n = 0;
        try {
          n = std::stoi(line);
        } catch (...) {
          s.known = false;
          return;
        }
        const int before = s.count;
        s.known = true;
        s.count = n;
        s.list.clear();
        while (std::getline(in, line)) {
          if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
          const auto tab = line.find('\t');
          s.list.emplace_back(line.substr(0, tab), tab == std::string::npos ? std::string() : line.substr(tab + 1));
        }
        s.checkedAt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (opt<bool>("updates", "notify", false) && n > 0 && n != before) {
          (void)process::runAsync(std::vector<std::string>{
              "notify-send", "-a", "Updates", "-i", "system-software-update",
              std::to_string(n) + (n == 1 ? " update" : " updates") + " waiting",
              "Click the updates module (or run: kusanagi updates upgrade)"});
        }
      }
    } // namespace

    void check() {
      State& s = state();
      if (s.checking) return;
      s.checking = true;
      const bool started = process::runAsync(
          std::vector<std::string>{cliCommand(), "updates", "raw"},
          process::RunCallbacks{
              .onExit = [](process::RunResult r) { DeferredCall::callLater([out = std::move(r.out)]() { finished(out); }); },
          }
      );
      if (!started) s.checking = false;
    }

    std::string count() {
      const State& s = state();
      if (s.known) return std::to_string(s.count);
      // Until this shell has checked, fall back to the count `kusanagi updates` caches on its first line.
      const std::string xdg = env("XDG_CACHE_HOME");
      std::ifstream f((xdg.empty() ? env("HOME") + "/.cache" : xdg) + "/kusanagi/updates");
      std::string line;
      if (f && std::getline(f, line)) {
        try {
          return std::to_string(std::stoi(line));
        } catch (...) {
        }
      }
      return "?";
    }

    Status status() {
      const State& s = state();
      return Status{.count = s.count, .known = s.known, .checking = s.checking, .checkedAt = s.checkedAt, .list = s.list};
    }

    void upgrade() {
      (void)process::runAsync(std::vector<std::string>{cliCommand(), "updates", "upgrade"});
      state().after.start(std::chrono::minutes(2), []() { check(); });
    }
  } // namespace updates

  namespace {
    std::atomic<bool> g_polkitRegistered{false};
  }
  void setPolkitRegistered(bool registered) { g_polkitRegistered = registered; }
  bool polkitRegistered() { return g_polkitRegistered; }

  std::string idleStatus(const IpcServices& sv) {
    if (!opt<bool>("idle", "enabled", false)) return "off";
    if (sv.caffeine != nullptr && sv.caffeine->enabled()) return "awake: caffeine";
    if (sv.gameMode != nullptr && sv.gameMode->active()) return "awake: game mode";
    if (opt<bool>("idle", "media", true) && sv.mpris != nullptr) {
      for (const auto& [_, p] : sv.mpris->players()) {
        if (p.playbackStatus == "Playing") return "awake: media playing";
      }
    }
    return "armed";
  }

  void registerIpc(IpcService& ipc, const IpcServices& sv) {
    namespace msg = kusanagi::cli::msg;

    ipc.bind(msg::preset, [](const std::string& args) -> std::string { return presets::command(args); });

    ipc.bind(msg::barLayout, [](const std::string& args) -> std::string {
      const auto [action, name] = split(args);
      if (action == "template") {
        if (bar_templates::find(name) == nullptr) {
          return "error: no bar layout named '" + name + "' (" + bar_templates::ids() + ")\n";
        }
        return bar_templates::apply(name) ? std::string() : "error: couldn't write settings.json\n";
      }
      if (action == "classic") return bar_templates::classic() ? std::string() : "error: couldn't write settings.json\n";
      if (action == "list" || action == "templates") return bar_templates::ids() + "\n";
      return "error: unknown action (template <id> | classic | list)\n";
    });

    ipc.bind(msg::idle, [sv](const std::string& args) -> std::string {
      const auto [action, rest] = split(args);
      if (action.empty() || action == "status") return idleStatus(sv) + "\n";
      if (action == "toggle") {
        // The config reload turns the idle behaviours on or off (config/kusanagi_import.cpp).
        const bool on = !opt<bool>("idle", "enabled", false);
        return editSettings([on](nlohmann::json& s) {
                 if (!s.contains("idle") || !s["idle"].is_object()) s["idle"] = nlohmann::json::object();
                 s["idle"]["enabled"] = on;
               })
            ? std::string()
            : "error: couldn't write settings.json\n";
      }
      return "error: unknown action (status | toggle)\n";
    });

    ipc.bind(msg::record, [](const std::string& args) -> std::string {
      const auto [action, rest] = split(args);
      if (action == "status") return recorder::mode() + "\n";
      if (action == "smart") {
        recorder::smart();
        return {};
      }
      if (action == "replay" || action == "save" || action == "record" || action == "stream" || action == "stop") {
        recorder::run(action);
        return {};
      }
      return "error: unknown action (replay | save | record | stream | stop | smart | status)\n";
    });

    ipc.bind(msg::updates, [](const std::string& args) -> std::string {
      const auto [action, rest] = split(args);
      if (action.empty() || action == "count") return updates::count() + "\n";
      if (action == "check") {
        updates::check();
        return {};
      }
      if (action == "upgrade") {
        updates::upgrade();
        return {};
      }
      return "error: unknown action (check | count | upgrade)\n";
    });

    ipc.bind(msg::launcherActions, [sv](const std::string&) -> std::string {
      if (sv.launcher != nullptr) (void)sv.launcher->actionsOfSelected();
      return {};
    });

    // One "<name> (<screen>): 40%" line per screen that can be dimmed.
    ipc.bind(msg::brightnessGet, [sv](const std::string&) -> std::string {
      if (sv.brightness == nullptr || !sv.brightness->available()) return "nothing to dim\n";
      std::string out;
      for (const auto& d : sv.brightness->displays()) {
        if (!d.controllable) continue;
        const bool builtIn = d.id.starts_with("eDP") || d.id.starts_with("LVDS") || d.id.starts_with("DSI");
        const std::string name = builtIn ? "Built-in display" : (d.label.empty() ? d.id : d.label);
        const std::string screen = name == d.id ? std::string() : d.id;
        out += name + (screen.empty() ? "" : " (" + screen + ")") + ": "
            + std::to_string(static_cast<int>(std::lround(d.brightness * 100.0F))) + "%\n";
      }
      return out.empty() ? "nothing to dim\n" : out;
    });
    ipc.bind(msg::brightnessDetect, [sv](const std::string&) -> std::string {
      if (sv.brightness != nullptr) sv.brightness->requestDdcRescan();
      return {};
    });

    // A one-word summary, then each connected device with its battery.
    ipc.bind(msg::bluetoothSummary, [sv](const std::string&) -> std::string {
      const bool enabled = opt<bool>("bluetooth", "enabled", true);
      if (!enabled || sv.bluetooth == nullptr || !sv.bluetooth->state().adapterPresent) {
        return enabled ? "no adapter (is bluetoothd running? kusanagi doctor)\n" : "off in Settings\n";
      }
      const auto& st = sv.bluetooth->state();
      std::vector<const BluetoothDeviceInfo*> connected;
      for (const auto& d : sv.bluetooth->devices()) {
        if (d.connected) connected.push_back(&d);
      }
      auto name = [](const BluetoothDeviceInfo* d) { return d->alias.empty() ? d->address : d->alias; };
      std::ranges::sort(connected, [&name](const auto* a, const auto* b) { return name(a) < name(b); });
      std::string out = st.rfkillSoftBlocked || st.rfkillHardBlocked ? "Blocked"
          : !st.powered                                             ? "Off"
          : connected.size() == 1                                   ? name(connected[0])
          : connected.size() > 1                                    ? std::to_string(connected.size()) + " devices"
                                                                    : "On";
      for (const auto* d : connected) {
        out += "\n" + name(d) + (d->hasBattery ? " " + std::to_string(d->batteryPercent) + "%" : "");
      }
      return out + "\n";
    });
  }

} // namespace kusanagi
