#include "shell/kusanagi/presets.h"

#include "core/process/process.h"
#include "shell/kusanagi/bar_templates.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "util/file_utils.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace kusanagi::presets {

  namespace {
    using json = nlohmann::json;
    using ojson = nlohmann::ordered_json;

    // One preset per line. Glyphs are written as \u escapes.
    constexpr const char* kBuiltin = R"json(
[
{"id":"kusanagi","name":"Kusanagi","note":"Translucent islands, pill workspaces, springy","look":{"radius":16,"animSpeed":1,"bounce":1,"backdrop":0.25,"borders":true,"borderAccent":false,"shadows":true,"palette":"wallpaper"},"bar":{"style":"islands","position":"top","height":28,"layout":"classic","accentLabels":false,"opacity":0.5,"radius":10,"fontSize":11,"outline":false,"clockBold":true,"hoverGrow":true,"modules":{"title":false,"media":true,"cpu":true,"ram":true,"gpu":false,"temp":false,"volume":true,"network":true,"tray":true,"power":true}},"workspaces":{"style":"pills","shown":5,"glow":true,"activeColor":"accent"},"panel":{"morph":"island"},"osd":{"style":"pill"},"notifications":{"style":"comfortable"},"launcher":{"layout":"list","iconSize":32}},
{"id":"minimal","name":"Minimal","note":"dwl-style flat bar: tag blocks, window title, status text","look":{"radius":6,"animSpeed":0.7,"bounce":0,"backdrop":0.15,"borders":true,"borderAccent":false,"shadows":false,"palette":"wallpaper"},"bar":{"style":"solid","position":"top","height":24,"layout":"classic","accentLabels":false,"opacity":1,"radius":0,"fontSize":11,"outline":false,"clockBold":false,"hoverGrow":false,"modules":{"title":true,"media":false,"cpu":true,"ram":true,"gpu":false,"temp":false,"volume":true,"network":false,"tray":true,"power":false}},"workspaces":{"style":"dwl","shown":9,"glow":false,"activeColor":"accent"},"panel":{"morph":"fade"},"osd":{"style":"minimal"},"notifications":{"style":"compact"},"launcher":{"layout":"list","iconSize":24}},
{"id":"glass","name":"Floating glass","note":"A see-through floating bar, accent outlines, bouncy","look":{"radius":22,"animSpeed":1,"bounce":1.4,"backdrop":0.3,"borders":true,"borderAccent":true,"shadows":true,"palette":"wallpaper"},"bar":{"style":"floating","position":"top","height":32,"layout":"classic","accentLabels":true,"opacity":0.35,"radius":16,"fontSize":11,"outline":false,"clockBold":true,"hoverGrow":true,"modules":{"title":false,"media":true,"cpu":true,"ram":true,"gpu":true,"temp":true,"volume":true,"network":true,"tray":true,"power":true}},"workspaces":{"style":"pills","shown":5,"glow":true,"activeColor":"accent"},"panel":{"morph":"island"},"osd":{"style":"pill"},"notifications":{"style":"comfortable"},"launcher":{"layout":"grid","iconSize":40}},
{"id":"zen","name":"Zen","note":"Bottom bar, clock left, dots centred, almost no stats, unhurried","look":{"radius":18,"animSpeed":1.4,"bounce":1.2,"backdrop":0.3,"borders":true,"borderAccent":false,"shadows":true,"palette":"wallpaper"},"bar":{"style":"islands","position":"bottom","height":30,"layout":"centered","accentLabels":false,"opacity":0.45,"radius":15,"fontSize":11,"outline":false,"clockBold":true,"hoverGrow":true,"modules":{"title":false,"media":true,"cpu":false,"ram":false,"gpu":false,"temp":false,"volume":true,"network":false,"tray":true,"power":true}},"workspaces":{"style":"dots","shown":5,"glow":true,"activeColor":"accent"},"panel":{"morph":"island"},"osd":{"style":"pill"},"notifications":{"style":"comfortable"},"launcher":{"layout":"grid","iconSize":48}},
{"id":"terminal","name":"Terminal","note":"Square everything, mono palette, roman tags, instant","look":{"radius":0,"animSpeed":0.7,"bounce":0,"backdrop":0.2,"borders":true,"borderAccent":true,"shadows":false,"palette":"mono"},"bar":{"style":"solid","position":"top","height":22,"layout":"classic","accentLabels":true,"opacity":0.92,"radius":0,"fontSize":11,"outline":false,"clockBold":false,"hoverGrow":false,"modules":{"title":true,"media":true,"cpu":true,"ram":true,"gpu":true,"temp":true,"volume":true,"network":true,"tray":true,"power":false}},"workspaces":{"style":"roman","shown":5,"glow":false,"activeColor":"text"},"panel":{"morph":"fade"},"osd":{"style":"minimal"},"notifications":{"style":"compact"},"launcher":{"layout":"list","iconSize":20}},
{"id":"neon","name":"Neon","note":"Tokyo Night, floating bar, glowing kanji tags, extra bouncy","look":{"radius":18,"animSpeed":1,"bounce":1.6,"backdrop":0.35,"borders":true,"borderAccent":true,"shadows":true,"palette":"tokyo-night"},"bar":{"style":"floating","position":"top","height":30,"layout":"classic","accentLabels":true,"opacity":0.55,"radius":15,"fontSize":11,"outline":false,"clockBold":true,"hoverGrow":true,"modules":{"title":false,"media":true,"cpu":true,"ram":true,"gpu":true,"temp":false,"volume":true,"network":true,"tray":true,"power":true}},"workspaces":{"style":"kanji","shown":5,"glow":true,"activeColor":"accent2"},"panel":{"morph":"island"},"osd":{"style":"pill"},"notifications":{"style":"comfortable"},"launcher":{"layout":"grid","iconSize":44}},
{"id":"paper","name":"Paper","note":"Light Latte palette, calm solid bar, soft and readable","look":{"radius":12,"animSpeed":1,"bounce":0.6,"backdrop":0.15,"borders":true,"borderAccent":false,"shadows":true,"palette":"catppuccin-latte"},"bar":{"style":"solid","position":"top","height":28,"layout":"classic","accentLabels":false,"opacity":0.94,"radius":0,"fontSize":11,"outline":false,"clockBold":true,"hoverGrow":false,"modules":{"title":true,"media":true,"cpu":false,"ram":false,"gpu":false,"temp":false,"volume":true,"network":true,"tray":true,"power":true}},"workspaces":{"style":"numbers","shown":5,"glow":false,"activeColor":"accent"},"panel":{"morph":"drop"},"osd":{"style":"pill"},"notifications":{"style":"comfortable"},"launcher":{"layout":"list","iconSize":28}},
{"id":"hud","name":"Gamer HUD","note":"Every stat on show (GPU, temps), dwl tags, instant, no frills","look":{"radius":8,"animSpeed":0.7,"bounce":0,"backdrop":0.2,"borders":true,"borderAccent":true,"shadows":false,"palette":"wallpaper"},"bar":{"style":"islands","position":"top","height":26,"layout":"classic","accentLabels":true,"opacity":0.7,"radius":6,"fontSize":11,"outline":true,"clockBold":true,"hoverGrow":false,"modules":{"title":false,"media":false,"cpu":true,"ram":true,"gpu":true,"temp":true,"volume":true,"network":true,"tray":true,"power":true}},"workspaces":{"style":"dwl","shown":5,"glow":false,"activeColor":"accent"},"panel":{"morph":"fade"},"osd":{"style":"minimal"},"notifications":{"style":"compact"},"launcher":{"layout":"list","iconSize":24}},
{"id":"nordic","name":"Nordic","note":"Nord palette, clear text bar, clock left + numbers centred","look":{"radius":14,"animSpeed":1,"bounce":0.8,"backdrop":0.25,"borders":false,"borderAccent":false,"shadows":true,"palette":"nord"},"bar":{"style":"clear","position":"top","height":28,"layout":"centered","accentLabels":true,"opacity":0.5,"radius":10,"fontSize":12,"outline":false,"clockBold":true,"hoverGrow":false,"modules":{"title":false,"media":true,"cpu":true,"ram":true,"gpu":false,"temp":false,"volume":true,"network":true,"tray":true,"power":true}},"workspaces":{"style":"numbers","shown":5,"glow":false,"activeColor":"accent"},"panel":{"morph":"drop"},"osd":{"style":"minimal"},"notifications":{"style":"comfortable"},"launcher":{"layout":"list","iconSize":32}},
{"id":"ink","name":"Ink","note":"Kanagawa colours, kanji tags, solid bottom bar, brushed calm","look":{"radius":10,"animSpeed":1.2,"bounce":0.5,"backdrop":0.3,"borders":true,"borderAccent":false,"shadows":true,"palette":"kanagawa"},"bar":{"style":"solid","position":"bottom","height":28,"layout":"classic","accentLabels":false,"opacity":0.9,"radius":0,"fontSize":11,"outline":false,"clockBold":false,"hoverGrow":false,"modules":{"title":true,"media":true,"cpu":false,"ram":false,"gpu":false,"temp":false,"volume":true,"network":false,"tray":true,"power":true}},"workspaces":{"style":"kanji","shown":5,"glow":false,"activeColor":"accent"},"panel":{"morph":"island"},"osd":{"style":"minimal"},"notifications":{"style":"comfortable"},"launcher":{"layout":"list","iconSize":28}},
{"id":"aurora","name":"Aurora","note":"glass islands, gradient clock, Ros\u00e9 Pine, smooth springs","look":{"radius":18,"animSpeed":1,"bounce":1,"backdrop":0.3,"borders":true,"borderAccent":false,"shadows":true,"palette":"rose-pine"},"barsTemplate":"aurora","panel":{"morph":"island","tileStyle":"cards","tileColumns":4,"order":["media","weather","tiles","sliders","stats"]},"osd":{"style":"pill","position":"top"},"notifications":{"style":"comfortable","position":"top-right"},"lock":{"style":"card"},"power":{"style":"row"},"launcher":{"style":"spotlight","layout":"list","iconSize":32}},
{"id":"material","name":"Material You","note":"tonal accent chips, wallpaper colours, bouncy","look":{"radius":24,"animSpeed":1,"bounce":1.8,"backdrop":0.25,"borders":false,"borderAccent":false,"shadows":true,"palette":"wallpaper"},"barsTemplate":"material","panel":{"morph":"drop","tileStyle":"pills","tileColumns":2,"sliderStyle":"slim","header":"compact","order":["sliders","tiles","media","stats"]},"osd":{"style":"pill","position":"bottom"},"notifications":{"style":"minimal","position":"top-center"},"lock":{"style":"card"},"power":{"style":"tiles"},"launcher":{"style":"fullscreen","layout":"grid","iconSize":56}},
{"id":"candy","name":"Candy","note":"colourful segments, Catppuccin Mocha, playful","look":{"radius":16,"animSpeed":1.25,"bounce":2,"backdrop":0.25,"borders":true,"borderAccent":true,"shadows":true,"palette":"catppuccin-mocha"},"barsTemplate":"candy","panel":{"morph":"island"},"osd":{"style":"box"},"notifications":{"style":"accent","position":"top-right"},"lock":{"style":"stacked"},"power":{"style":"tiles"},"launcher":{"style":"card","layout":"grid","iconSize":40}},
{"id":"notch","name":"Notch","note":"a black island at the top, mono, side-sheet panel","look":{"radius":18,"animSpeed":1,"bounce":0.6,"backdrop":0.3,"borders":false,"borderAccent":false,"shadows":true,"palette":"mono"},"barsTemplate":"notch","panel":{"morph":"sheet","tileStyle":"icons","tileColumns":6,"sliderStyle":"slim","header":"hidden","tabs":true,"order":["tiles","sliders","media","weather"]},"osd":{"style":"box"},"notifications":{"style":"minimal","position":"top-center"},"lock":{"style":"minimal"},"power":{"style":"pill"},"launcher":{"style":"spotlight","layout":"list","iconSize":28}},
{"id":"cyber","name":"Cyber","note":"neon slants, Tokyo Night, kanji, snappy","look":{"radius":2,"animSpeed":0.6,"bounce":0.3,"backdrop":0.35,"borders":true,"borderAccent":true,"shadows":false,"palette":"tokyo-night"},"barsTemplate":"cyber","panel":{"morph":"fade"},"osd":{"style":"minimal","position":"top"},"notifications":{"style":"accent","position":"bottom-right"},"lock":{"style":"terminal"},"power":{"style":"list"},"launcher":{"style":"side","layout":"list","iconSize":28}},
{"id":"win11","name":"Windows 11","note":"centred taskbar, start-menu launcher, side sheet","look":{"radius":8,"animSpeed":0.8,"bounce":0.4,"backdrop":0.2,"borders":true,"borderAccent":false,"shadows":true,"palette":"wallpaper"},"barsTemplate":"win11","panel":{"morph":"sheet","tileStyle":"pills","tileColumns":3,"sliderStyle":"slim","header":"compact","order":["tiles","sliders","media"]},"osd":{"style":"pill","position":"bottom"},"notifications":{"style":"comfortable","position":"bottom-right"},"lock":{"style":"split"},"power":{"style":"list"},"launcher":{"style":"card","layout":"grid","iconSize":40,"position":"center"}},
{"id":"zen2","name":"Zen pill","note":"one floating pill at the bottom, Gentle motion, Everforest","look":{"radius":20,"animSpeed":1.4,"bounce":0.4,"backdrop":0.3,"borders":true,"borderAccent":false,"shadows":true,"palette":"everforest"},"barsTemplate":"zen","panel":{"morph":"fade"},"osd":{"style":"minimal","position":"bottom"},"notifications":{"style":"minimal","position":"bottom-center"},"lock":{"style":"minimal"},"power":{"style":"pill"},"launcher":{"style":"spotlight","layout":"list","iconSize":28}},
{"id":"powerline","name":"Powerline","note":"agnoster arrows: touching coloured segments, square and quick","look":{"radius":4,"animSpeed":0.7,"bounce":0.3,"backdrop":0.2,"borders":true,"borderAccent":false,"shadows":false,"palette":"wallpaper"},"barsTemplate":"powerline","panel":{"morph":"fade"},"osd":{"style":"minimal"},"notifications":{"style":"compact"},"launcher":{"layout":"list","iconSize":24}},
{"id":"sidebar","name":"Sidebar","note":"a vertical bar on the left: pills, turned clock, stacked stats","look":{"radius":14,"animSpeed":1,"bounce":1,"backdrop":0.25,"borders":true,"borderAccent":false,"shadows":true,"palette":"wallpaper"},"barsTemplate":"sidebar","panel":{"morph":"fade"},"osd":{"style":"pill","position":"right"},"notifications":{"style":"comfortable"},"launcher":{"layout":"grid","iconSize":40}},
{"id":"dock","name":"Dock","note":"slim top strip + a floating dock of open apps at the bottom","look":{"radius":18,"animSpeed":1,"bounce":1.2,"backdrop":0.3,"borders":true,"borderAccent":false,"shadows":true,"palette":"wallpaper"},"barsTemplate":"dock","panel":{"morph":"drop"},"osd":{"style":"pill"},"notifications":{"style":"comfortable"},"launcher":{"layout":"grid","iconSize":48}}
]
)json";
    // The sections a preset may touch, and which of their keys make up a look. Snapshots keep this order.
    constexpr const char* kLookKeys = R"json({"look":["radius","animSpeed","bounce","backdrop","borders","borderAccent","shadows","palette","font","accent"],"bar":["style","position","height","layout","accentLabels","opacity","radius","fontSize","outline","clockBold","hoverGrow","clock","modules"],"workspaces":["style","shown","glow","activeColor","icons"],"panel":["morph","opacity","tileStyle","tileColumns","sliderStyle","header","tabs","order"],"osd":["style","position"],"notifications":["style","position"],"launcher":["style","position","layout","iconSize"],"lock":["style"],"power":["style"]})json";
    // Looks made before these settings existed put them back to these defaults.
    constexpr const char* kStyleDefaults = R"json({"launcher":{"style":"card","position":"upper"},"osd":{"position":"top"},"notifications":{"position":"top-right"},"lock":{"style":"center"},"power":{"style":"row"},"panel":{"tileStyle":"cards","tileColumns":4,"sliderStyle":"thick","header":"big","tabs":true,"order":["tiles","sliders","media","weather","stats"]}})json";
    // Defaults of those sections. A snapshot takes settings.json's value, else these.
    constexpr const char* kLookDefaults = R"json(
{
"look":{"font":"JetBrainsMono Nerd Font","radius":16,"accent":"","palette":"wallpaper","animSpeed":1,"shadows":true,"preload":true,"bounce":1,"backdrop":0.25,"borders":true,"borderAccent":false},
"bar":{"style":"islands","position":"top","height":28,"layout":"classic","accentLabels":false,"opacity":0.5,"radius":10,"fontSize":11,"outline":false,"clock":"HH:mm","clockBold":true,"trayIconSize":14,"hoverGrow":true,"scrollClock":"volume","scrollStats":"volume","volumeStep":5,"mediaPopup":true,"marquee":true,"mediaWidth":20,"titleWidth":60,"modules":{"title":false,"media":true,"cpu":true,"ram":true,"gpu":false,"temp":false,"volume":true,"network":true,"tray":true,"power":true}},
"workspaces":{"style":"pills","shown":5,"glow":true,"icons":"","activeColor":"accent"},
"panel":{"opacity":0.95,"width":560,"defaultTab":0,"showMedia":true,"showStats":true,"showWeather":true,"morph":"island","tileStyle":"cards","tileColumns":4,"sliderStyle":"thick","header":"big","tabs":true,"order":["tiles","sliders","media","weather","stats"],"tiles":["nightlight","dnd","mic","gamemode","screenshot","record","colorpicker","wallpaper"]},
"osd":{"position":"top","timeout":1400,"volume":true,"mic":true,"gamemode":true,"brightness":true,"style":"pill","showValue":true},
"notifications":{"position":"top-right","timeout":5000,"max":5,"style":"comfortable","progress":true,"images":true},
"launcher":{"style":"card","position":"upper","width":640,"rows":7,"descriptions":true,"sortByUsage":true,"terminal":"foot","layout":"list","iconSize":32,"commands":true,"webSearch":true,"searchEngine":"https://duckduckgo.com/?q=%s"},
"lock":{"engine":"hyprlock","style":"center","blur":0.8,"dim":0.35,"clock":"HH:mm","avatar":true,"media":true,"greeting":""},
"power":{"style":"row"}
}
)json";
    // Click and scroll actions the bar handles itself. Anything else runs in a shell.
    constexpr const char* kBuiltinActions = R"json(["panel","launcher","settings","power","wallpaper","clipboard","lock","notifs","dnd","caffeine","gamemode","preset","alt","media","volume","mic","workspace","none"])json";
    constexpr const char* kModules[] = {"title", "media", "cpu", "ram", "gpu", "temp", "volume", "network", "tray", "power"};

    std::string& lastAppliedRef() {
      static std::string s;
      return s;
    }

    // False for null, false, 0 and "", like JavaScript.
    bool truthy(const json& v) {
      if (v.is_null()) return false;
      if (v.is_boolean()) return v.get<bool>();
      if (v.is_number()) return v.get<double>() != 0.0;
      if (v.is_string()) return !v.get_ref<const std::string&>().empty();
      return true;
    }

    std::string lower(std::string_view s) {
      std::string out(s);
      std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return out;
    }

    std::string trim(std::string_view s) {
      const auto b = s.find_first_not_of(" \t\r\n\f\v");
      if (b == std::string_view::npos) return {};
      const auto e = s.find_last_not_of(" \t\r\n\f\v");
      return std::string(s.substr(b, e - b + 1));
    }

    std::filesystem::path configPath(const char* file) { return std::filesystem::path(FileUtils::configDir()) / file; }

    json readJsonFile(const std::filesystem::path& path) {
      std::ifstream in(path);
      if (!in) return json();
      return json::parse(in, nullptr, false);
    }

    bool writeTextFile(const std::filesystem::path& path, const std::string& text) {
      std::error_code ec;
      std::filesystem::create_directories(path.parent_path(), ec);
      const auto tmp = path.string() + ".tmp";
      {
        std::ofstream out(tmp);
        if (!out) return false;
        out << text;
      }
      std::filesystem::rename(tmp, path, ec);
      return !ec;
    }

    // Writes presets.json's "saved" list, keeping any other keys as they are.
    bool writeSaved(const json& list) {
      json doc = readJsonFile(configPath("presets.json"));
      if (!doc.is_object()) doc = json::object();
      doc["saved"] = list;
      return writeTextFile(configPath("presets.json"), doc.dump(4) + "\n");
    }

    json withoutName(const json& list, const std::string& name) {
      json out = json::array();
      for (const auto& s : list) {
        if (!(s.is_object() && s.value("name", "") == name)) out.push_back(s);
      }
      return out;
    }

    const json& builtinActions() {
      static const json a = json::parse(kBuiltinActions, nullptr, false);
      return a;
    }
    bool isBuiltinAction(const std::string& a) {
      const std::string head = a.substr(0, a.find(':'));
      const auto& list = builtinActions();
      return std::ranges::any_of(list, [&head](const json& x) { return x.is_string() && x.get<std::string>() == head; });
    }
    // A shell command hiding in a click or scroll action: a string that isn't built in, or an argv array.
    bool isCommandAction(const json& a) {
      return a.is_array() || (a.is_string() && !a.get_ref<const std::string&>().empty() && !isBuiltinAction(a.get<std::string>()));
    }
    constexpr const char* kActionKeys[] = {"click", "rightClick", "middleClick", "scrollUp", "scrollDown"};

    std::string joinArgv(const json& a) {
      std::string out;
      for (std::size_t i = 0; i < a.size(); ++i) {
        if (i > 0) out += ' ';
        out += a[i].is_string() ? a[i].get<std::string>() : a[i].is_null() ? std::string() : a[i].dump();
      }
      return out;
    }

    // section.key from settings.json, or its default when the file doesn't have it.
    json effective(const json& s, const std::string& section, const std::string& key) {
      if (s.is_object()) {
        const auto sec = s.find(section);
        if (sec != s.end() && sec->is_object()) {
          const auto it = sec->find(key);
          if (it != sec->end() && !it->is_null()) return *it;
        }
      }
      static const json defaults = json::parse(kLookDefaults, nullptr, false);
      if (defaults.contains(section) && defaults[section].contains(key)) return defaults[section][key];
      return json();
    }

    std::string nowFormatted(const char* fmt, bool utc) {
      const std::time_t t = std::time(nullptr);
      std::tm tm{};
      if (utc) gmtime_r(&t, &tm);
      else localtime_r(&t, &tm);
      char buf[64];
      std::strftime(buf, sizeof(buf), fmt, &tm);
      return buf;
    }

    // The snapshot with keys in lookKeys() order, so shared files stay stable.
    ojson snapshotOrdered(const std::string& name) {
      const json s = readJsonFile(configPath("settings.json"));
      ojson p;
      p["id"] = "user:" + name;
      p["name"] = name;
      p["note"] = "Saved " + nowFormatted("%-d %b, %H:%M", false);
      for (auto it = lookKeys().begin(); it != lookKeys().end(); ++it) {
        const std::string section = it.key();
        ojson sec = ojson::object();
        for (const auto& kj : it.value()) {
          const std::string k = kj.get<std::string>();
          if (k == "modules") {
            ojson mods = ojson::object();
            const json cur = effective(s, "bar", "modules");
            json defaults = json::parse(kLookDefaults, nullptr, false)["bar"]["modules"];
            for (const char* m : kModules) {
              const json v = cur.is_object() && cur.contains(m) && !cur[m].is_null() ? cur[m] : defaults[m];
              mods[m] = ojson::parse(v.dump());
            }
            sec["modules"] = std::move(mods);
          } else {
            sec[k] = ojson::parse(effective(s, section, k).dump());
          }
        }
        p[section] = std::move(sec);
      }
      if (s.is_object() && s.contains("bars") && s["bars"].is_array() && !s["bars"].empty()) {
        p["bars"] = ojson::parse(s["bars"].dump());
      }
      return p;
    }
  } // namespace

  const json& builtin() {
    static const json b = [] {
      json j = json::parse(kBuiltin, nullptr, false);
      return j.is_array() ? j : json::array();
    }();
    return b;
  }

  json saved() {
    const json doc = readJsonFile(configPath("presets.json"));
    if (doc.is_object() && doc.contains("saved") && doc["saved"].is_array()) return doc["saved"];
    return json::array();
  }

  json all() {
    json out = builtin();
    for (const auto& s : saved()) out.push_back(s);
    return out;
  }

  json find(std::string_view idOrName) {
    const std::string n = lower(idOrName);
    for (const auto& p : all()) {
      if (!p.is_object()) continue;
      const std::string id = p.contains("id") && p["id"].is_string() ? p["id"].get<std::string>() : "";
      const std::string name = p.contains("name") && p["name"].is_string() ? p["name"].get<std::string>() : "";
      if (lower(id) == n || lower(name) == n) return p;
    }
    return json();
  }

  const ojson& lookKeys() {
    static const ojson k = ojson::parse(kLookKeys, nullptr, false);
    return k;
  }

  const json& styleDefaults() {
    static const json d = json::parse(kStyleDefaults, nullptr, false);
    return d;
  }

  // A preset's bar layout: its own "bars", a template's, or none, meaning the classic bar from its "bar" options.
  json barsOf(const json& p) {
    if (!p.is_object()) return json::array();
    if (p.contains("bars") && truthy(p["bars"])) return p["bars"];
    if (p.contains("barsTemplate") && truthy(p["barsTemplate"]) && p["barsTemplate"].is_string()) {
      return bar_templates::bars(p["barsTemplate"].get<std::string>());
    }
    return json::array();
  }

  bool apply(const json& p) {
    if (!p.is_object()) return false;
    const auto& defs = styleDefaults();
    const bool ok = editSettings([&](json& s) {
      for (auto it = lookKeys().begin(); it != lookKeys().end(); ++it) {
        const std::string section = it.key();
        const bool has = p.contains(section) && truthy(p[section]);
        const bool hasDefaults = defs.contains(section);
        // Sections a preset doesn't mention still get their style defaults, so older looks reset newer choices.
        if (!has && !hasDefaults) continue;
        json src = hasDefaults ? defs[section] : json::object();
        if (has && p[section].is_object()) {
          for (const auto& [k, v] : p[section].items()) src[k] = v;
        }
        if (!s.contains(section) || !s[section].is_object()) s[section] = json::object();
        json& target = s[section];
        for (const auto& kj : it.value()) {
          const std::string k = kj.get<std::string>();
          if (!src.contains(k)) continue;
          if (k == "modules") {
            if (!target.contains("modules") || !target["modules"].is_object()) target["modules"] = json::object();
            if (src[k].is_object()) {
              for (const auto& [m, v] : src[k].items()) target["modules"][m] = v;
            }
          } else {
            target[k] = src[k];
          }
        }
      }
      // Every preset decides the bar layout too. A classic one brings the classic bar back.
      s["bars"] = barsOf(p);
    });
    if (ok) {
      const json id = p.value("id", json());
      lastAppliedRef() = truthy(id) && id.is_string() ? id.get<std::string>() : p.value("name", "");
    }
    return ok;
  }

  bool applyNamed(std::string_view name) {
    const json p = find(name);
    return !p.is_null() && apply(p);
  }

  std::string next() {
    const json list = all();
    if (list.empty()) return {};
    std::size_t i = list.size() - 1; // none applied yet: start with the first
    for (std::size_t j = 0; j < list.size(); ++j) {
      const json id = list[j].value("id", json());
      const std::string key = truthy(id) && id.is_string() ? id.get<std::string>() : list[j].value("name", "");
      if (key == lastAppliedRef()) {
        i = j;
        break;
      }
    }
    const json& p = list[(i + 1) % list.size()];
    apply(p);
    return lastAppliedRef();
  }

  const std::string& lastApplied() { return lastAppliedRef(); }

  json snapshot(const std::string& name) { return json::parse(snapshotOrdered(name).dump()); }

  bool save(std::string name) {
    json list = saved();
    name = trim(name);
    if (name.empty()) name = "My look " + std::to_string(list.size() + 1);
    list = withoutName(list, name);
    list.push_back(json::parse(snapshotOrdered(name).dump()));
    if (!writeSaved(list)) return false;
    lastAppliedRef() = "user:" + name;
    return true;
  }

  bool remove(const std::string& name) {
    json list = saved();
    list = withoutName(list, name);
    return writeSaved(list);
  }

  bool keep(const json& look) {
    if (!look.is_object()) return false;
    const std::string name = look.value("name", "");
    json list = saved();
    list = withoutName(list, name);
    list.push_back(look);
    return writeSaved(list);
  }

  std::string looksDir() {
    const char* home = std::getenv("HOME");
    return std::string(home != nullptr ? home : "") + "/kusanagi-looks";
  }

  // File-name slug: ASCII letters, digits, "_" and "-" kept, spaces to dashes, lowercase. "my-look" if empty.
  std::string slug(std::string_view n) {
    std::string kept;
    for (const char c : trim(n)) {
      const auto u = static_cast<unsigned char>(c);
      if (u < 0x80 && (std::isalnum(u) || c == '_' || c == '-' || c == ' ')) kept += c;
    }
    std::string out;
    bool inSpace = false;
    for (const char c : kept) {
      if (c == ' ') {
        if (!inSpace) out += '-';
        inSpace = true;
      } else {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        inSpace = false;
      }
    }
    return out.empty() ? "my-look" : out;
  }

  // { kusanagi: 1, kind: "look", name, note, created, look }. Wallpaper and pinned apps stay personal and
  // aren't included.
  ojson lookDoc(const std::string& name) {
    ojson p = snapshotOrdered(name);
    p.erase("id");
    ojson doc;
    doc["kusanagi"] = 1;
    doc["kind"] = "look";
    doc["name"] = p["name"];
    doc["note"] = p["note"];
    // ISO 8601 in UTC with milliseconds.
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    char ms[8];
    std::snprintf(ms, sizeof(ms), ".%03dZ", static_cast<int>(millis));
    doc["created"] = nowFormatted("%Y-%m-%dT%H:%M:%S", true) + ms;
    doc["look"] = std::move(p);
    return doc;
  }

  std::string exportLook(std::string name, bool toClipboard) {
    name = trim(name);
    if (name.empty()) name = "My look";
    const std::string text = lookDoc(name).dump(2);
    if (toClipboard) {
      (void)process::runAsync(std::vector<std::string>{"sh", "-c", "printf '%s' \"$1\" | wl-copy", "sh", text});
      return "clipboard";
    }
    const std::string path = looksDir() + "/" + slug(name) + ".kusanagi";
    return writeTextFile(path, text) ? path : std::string();
  }

  ReadResult readLook(std::string_view text) {
    ReadResult r;
    json d = json::parse(text, nullptr, false);
    if (d.is_discarded()) {
      r.error = "That isn't a Kusanagi look (not JSON).";
      return r;
    }
    if (!d.is_object() || !d.contains("kusanagi") || !d.contains("look") || !truthy(d["look"]) || !d["look"].is_object()) {
      r.error = "That isn't a Kusanagi look.";
      return r;
    }
    json look = d["look"];
    auto pick = [&](const char* key, const char* fallback) -> json {
      if (truthy(d.value(key, json()))) return d[key];
      if (truthy(look.value(key, json()))) return look[key];
      return fallback;
    };
    look["name"] = pick("name", "Imported look");
    look["note"] = pick("note", "imported");
    look["id"] = "user:" + (look["name"].is_string() ? look["name"].get<std::string>() : look["name"].dump());
    r.commands = commandsIn(look);
    r.look = std::move(look);
    return r;
  }

  ReadResult readLookFile(std::string_view pathIn) {
    std::string path(pathIn);
    if (path.starts_with("file://")) path.erase(0, 7);
    std::ifstream in(path);
    std::stringstream ss;
    if (in) ss << in.rdbuf();
    return readLook(ss.str());
  }

  // what a shared look would run on this machine: custom modules' commands, and click/scroll
  // actions that aren't built in (those run in a shell)
  std::vector<std::string> commandsIn(const json& look) {
    std::vector<std::string> out;
    std::function<void(const json&)> walk = [&](const json& e) {
      if (!e.is_object() && !e.is_array()) return;
      if (e.is_object()) {
        if (e.value("type", json()) == "custom" && e.contains("exec") && truthy(e["exec"])) {
          out.push_back(e["exec"].is_string() ? e["exec"].get<std::string>() : e["exec"].dump());
        }
        for (const char* k : kActionKeys) {
          if (!e.contains(k)) continue;
          const json& a = e[k];
          if (a.is_string() && isCommandAction(a)) out.push_back(a.get<std::string>());
          else if (a.is_array()) out.push_back(joinArgv(a));
        }
      }
      for (const auto& v : e) {
        if (v.is_object() || v.is_array()) walk(v);
      }
    };
    if (look.is_object() && look.contains("bars") && look["bars"].is_array()) {
      for (const auto& b : look["bars"]) walk(b);
    }
    return out;
  }

  // the same look with every command taken out (custom modules dropped, shell actions cleared)
  json withoutCommands(const json& look) {
    json copy = look;
    std::function<json(json)> clean = [&](json e) -> json {
      if (e.is_array()) {
        json out = json::array();
        for (auto& x : e) {
          if (x.is_object() && x.value("type", json()) == "custom") continue;
          out.push_back(clean(std::move(x)));
        }
        return out;
      }
      if (!e.is_object()) return e;
      for (const char* k : kActionKeys) {
        if (e.contains(k) && isCommandAction(e[k])) e.erase(k);
      }
      for (auto& [k, v] : e.items()) v = clean(v);
      return e;
    };
    if (!copy.is_object()) return copy;
    copy["bars"] = clean(copy.contains("bars") ? copy["bars"] : json::array());
    return copy;
  }

  std::string command(std::string_view argsIn) {
    const std::string args = trim(argsIn);
    const auto space = args.find(' ');
    const std::string action = args.substr(0, space);
    const std::string arg = space == std::string::npos ? std::string() : trim(std::string_view(args).substr(space + 1));
    if (action == "apply") {
      if (arg.empty()) return "error: preset apply needs a name (kusanagi preset list)\n";
      const json p = find(arg);
      if (p.is_null()) return "error: no preset named '" + arg + "' (kusanagi preset list)\n";
      return apply(p) ? std::string() : "error: couldn't write settings.json\n";
    }
    if (action == "next") {
      next();
      return {};
    }
    if (action == "list" || action.empty()) {
      std::string out;
      for (const auto& p : all()) {
        const json id = p.value("id", json());
        const std::string name = p.value("name", "");
        out += "  " + (truthy(id) && id.is_string() ? id.get<std::string>() : name) + "\t" + name + " — " + p.value("note", "") + "\n";
      }
      return out;
    }
    // Import applies and keeps a look file, but refuses one that runs commands. Those go through the presets
    // settings page, which shows the commands before anything happens.
    if (action == "export" || action == "exportLook") {
      exportLook(arg, false);
      return looksDir() + "/" + slug(arg) + ".kusanagi\n";
    }
    if (action == "import" || action == "importLook") {
      const ReadResult r = readLookFile(arg);
      if (!r.error.empty()) return r.error + "\n";
      if (!r.commands.empty()) return "this look runs commands — open Settings → Presets → Share a look to review it\n";
      keep(r.look);
      apply(r.look);
      return "applied " + r.look.value("name", "") + "\n";
    }
    return "error: unknown action (apply <name> | next | list | export <name> | import <file>)\n";
  }

  int runCli(int argc, char* argv[]) {
    std::string args;
    for (int i = 2; i < argc; ++i) {
      if (!args.empty()) args += ' ';
      args += argv[i];
    }
    const std::string out = command(args);
    std::fputs(out.c_str(), stdout);
    return out.starts_with("error:") ? 1 : 0;
  }

} // namespace kusanagi::presets
