#include "config/kusanagi_import.h"

#include "config/config_types.h"
#include "core/log.h"
#include "shell/bar/widgets/kusanagi_box.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "ui/palette.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace kusanagi::config {

  namespace {

    constexpr Logger kLog("kusanagi");
    constexpr double kLockBlurScale = 0.3; // lock.blur to lockscreen.blur_intensity, tuned to match the classic lock.
    using json = nlohmann::json;

    std::optional<json> readJson(const std::filesystem::path& path) {
      std::ifstream in(path);
      if (!in) {
        return std::nullopt;
      }
      try {
        std::stringstream buf;
        buf << in.rdbuf();
        auto j = json::parse(buf.str());
        return j.is_object() ? std::optional<json>(std::move(j)) : std::nullopt;
      } catch (const std::exception& e) {
        kLog.warn("{}: {}", path.string(), e.what());
        return std::nullopt;
      }
    }

    std::string readLine(const std::filesystem::path& path) {
      std::ifstream in(path);
      std::string line;
      if (in) {
        std::getline(in, line);
      }
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
      }
      return line;
    }

    // settings.json section.key, or the fallback when it's missing or has the wrong type.
    template <typename T> T get(const json& root, const char* section, const char* key, T fallback) {
      auto s = root.find(section);
      if (s == root.end() || !s->is_object()) {
        return fallback;
      }
      auto k = s->find(key);
      if (k == s->end() || k->is_null()) {
        return fallback;
      }
      try {
        return k->get<T>();
      } catch (const std::exception&) {
        return fallback;
      }
    }

    // Colours

    struct Rgb {
      double r = 0, g = 0, b = 0;
    };

    std::optional<Rgb> parseHex(std::string s) {
      if (!s.empty() && s[0] == '#') {
        s.erase(0, 1);
      }
      if (s.size() == 8) {
        s = s.substr(2); // #aarrggbb: drop the alpha.
      }
      if (s.size() != 6) {
        return std::nullopt;
      }
      try {
        const unsigned v = std::stoul(s, nullptr, 16);
        return Rgb{((v >> 16) & 0xff) / 255.0, ((v >> 8) & 0xff) / 255.0, (v & 0xff) / 255.0};
      } catch (const std::exception&) {
        return std::nullopt;
      }
    }

    std::string hex(const Rgb& c, std::optional<double> alpha = std::nullopt) {
      auto b = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
      char buf[16];
      if (alpha) {
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", b(c.r), b(c.g), b(c.b), b(*alpha));
      } else {
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", b(c.r), b(c.g), b(c.b));
      }
      return buf;
    }

    Rgb mix(const Rgb& a, const Rgb& b, double t) {
      return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
    }

    // Kusanagi's resolved colours.
    struct Look {
      Rgb text, dim, danger, accent, accent2, bg, card, faint, ok;
    };

    struct Preset {
      const char* bg;
      const char* card;
      const char* text;
      const char* dim;
      const char* faint;
      const char* accent;
      const char* accent2;
      const char* danger;
      const char* ok;
    };

    const std::map<std::string, Preset>& presets() {
      static const std::map<std::string, Preset> p{
          {"catppuccin-mocha",
           {"#1e1e2e", "#313244", "#cdd6f4", "#a6adc8", "#585b70", "#cba6f7", "#f5c2e7", "#f38ba8", "#a6e3a1"}},
          {"catppuccin-latte",
           {"#eff1f5", "#ccd0da", "#4c4f69", "#6c6f85", "#9ca0b0", "#8839ef", "#ea76cb", "#d20f39", "#40a02b"}},
          {"gruvbox",
           {"#1d2021", "#32302f", "#ebdbb2", "#a89984", "#504945", "#fabd2f", "#fe8019", "#fb4934", "#b8bb26"}},
          {"nord", {"#2e3440", "#3b4252", "#eceff4", "#d8dee9", "#4c566a", "#88c0d0", "#81a1c1", "#bf616a", "#a3be8c"}},
          {"rose-pine",
           {"#191724", "#26233a", "#e0def4", "#908caa", "#403d52", "#ebbcba", "#c4a7e7", "#eb6f92", "#9ccfd8"}},
          {"tokyo-night",
           {"#1a1b26", "#24283b", "#c0caf5", "#a9b1d6", "#414868", "#7aa2f7", "#bb9af7", "#f7768e", "#9ece6a"}},
          {"everforest",
           {"#272e33", "#374145", "#d3c6aa", "#9da9a0", "#4f5b58", "#a7c080", "#83c092", "#e67e80", "#a7c080"}},
          {"kanagawa",
           {"#1f1f28", "#2a2a37", "#dcd7ba", "#c8c093", "#54546d", "#7e9cd8", "#957fb8", "#e82424", "#98bb6c"}},
          {"mono", {"#0e0e0e", "#1c1c1c", "#e6e6e6", "#9a9a9a", "#3a3a3a", "#e6e6e6", "#b0b0b0", "#ff5f5f", "#8fd18f"}},
      };
      return p;
    }

    Look resolveLook(const json& settings, const std::optional<json>& colors) {
      auto c = [&](const char* key, const char* fallback) {
        if (colors) {
          if (auto it = colors->find(key); it != colors->end() && it->is_string()) {
            if (auto v = parseHex(it->get<std::string>())) {
              return *v;
            }
          }
        }
        return *parseHex(fallback);
      };
      // Fallbacks for keys colors.json doesn't have.
      Look l{c("text", "#ffffff"),   c("textDim", "#c2c2c2"), c("danger", "#ff003c"),
             c("accent", "#ffffff"), c("accent2", "#ffffff"), c("bgPanel", "#050505"),
             c("bgCard", "#0d0d0d"), c("textFaint", "#4a4a4a"), c("ok", "#00ff9c")};
      const std::string palette = get<std::string>(settings, "look", "palette", "wallpaper");
      if (auto it = presets().find(palette); it != presets().end()) {
        const Preset& p = it->second;
        l = {*parseHex(p.text),  *parseHex(p.dim),  *parseHex(p.danger), *parseHex(p.accent), *parseHex(p.accent2),
             *parseHex(p.bg),    *parseHex(p.card), *parseHex(p.faint),  *parseHex(p.ok)};
      }
      if (auto accent = parseHex(get<std::string>(settings, "look", "accent", ""))) {
        l.accent = *accent;
      }
      return l;
    }

    // Writes <config>/palettes/kusanagi.json in the custom-palette format. Dark and light get the
    // same colours: a Kusanagi palette is light or dark by its colours alone.
    void writePalette(const std::filesystem::path& configDir, const Look& l) {
      json mode = json::object();
      mode["mPrimary"] = hex(l.accent);
      mode["mOnPrimary"] = hex(l.bg);
      mode["mSecondary"] = hex(l.accent2);
      mode["mOnSecondary"] = hex(l.bg);
      mode["mTertiary"] = hex(l.ok);
      mode["mOnTertiary"] = hex(l.bg);
      mode["mError"] = hex(l.danger);
      mode["mOnError"] = hex(l.bg);
      mode["mSurface"] = hex(l.bg);
      mode["mOnSurface"] = hex(l.text);
      mode["mSurfaceVariant"] = hex(l.card);
      mode["mOnSurfaceVariant"] = hex(l.dim);
      mode["mOutline"] = hex(l.faint);
      mode["mShadow"] = "#000000";
      mode["mHover"] = hex(mix(l.card, l.text, 0.06));
      mode["mOnHover"] = hex(l.text);
      // The palette format wants terminal colours with every palette; its templates use them.
      json ansi = json::object();
      ansi["black"] = hex(l.card);
      ansi["red"] = hex(l.danger);
      ansi["green"] = hex(l.ok);
      ansi["yellow"] = "#e8be62";
      ansi["blue"] = hex(l.accent2);
      ansi["magenta"] = hex(l.accent);
      ansi["cyan"] = hex(mix(l.accent2, l.ok, 0.5));
      ansi["white"] = hex(l.text);
      json terminal = json::object();
      terminal["normal"] = ansi;
      terminal["bright"] = ansi;
      terminal["foreground"] = hex(l.text);
      terminal["background"] = hex(l.bg);
      terminal["cursor"] = hex(l.accent);
      terminal["cursorText"] = hex(l.bg);
      terminal["selectionFg"] = hex(l.text);
      terminal["selectionBg"] = hex(l.card);
      mode["terminal"] = terminal;
      json root = json::object();
      root["dark"] = mode;
      root["light"] = mode;
      const std::string text = root.dump(2);

      const auto dir = configDir / "palettes";
      const auto path = dir / "kusanagi.json";
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
      std::ifstream old(path);
      std::stringstream oldText;
      oldText << old.rdbuf();
      if (oldText.str() == text) {
        return; // Unchanged, so leave the file alone.
      }
      std::ofstream out(path);
      out << text;
    }

    // Helpers

    toml::table& tbl(toml::table& parent, std::string_view key) {
      if (auto* t = parent[key].as_table()) {
        return *t;
      }
      parent.insert_or_assign(key, toml::table{});
      return *parent[key].as_table();
    }

    std::string expandHome(std::string p) {
      if (!p.empty() && p[0] == '~') {
        const char* home = std::getenv("HOME");
        p = std::string(home != nullptr ? home : "") + p.substr(1);
      }
      return p;
    }

    // Kusanagi positions to the OSD and notification anchors.
    std::string osdPosition(const std::string& pos) {
      if (pos == "bottom") return "bottom_center";
      if (pos == "left") return "center_left";
      if (pos == "right") return "center_right";
      return "top_center";
    }

    std::string notificationPosition(std::string pos) {
      std::replace(pos.begin(), pos.end(), '-', '_');
      return pos;
    }

    // Bars

    // Colour token ("accent", "bg/0.7", "#rrggbb", "transparent") to "#rrggbbaa".
    std::string tokenHex(const json& tokJson, const Look& l, const std::string& fallback = "") {
      if (!tokJson.is_string()) {
        return fallback;
      }
      std::string tok = tokJson.get<std::string>();
      if (tok.empty()) {
        return fallback;
      }
      double a = 1.0;
      if (const auto slash = tok.rfind('/'); slash != std::string::npos && slash > 0) {
        try {
          a = std::stod(tok.substr(slash + 1));
        } catch (...) {
        }
        tok = tok.substr(0, slash);
      }
      if (tok == "transparent" || tok == "none") {
        return "#00000000";
      }
      const std::map<std::string, Rgb> named{
          {"accent", l.accent}, {"accent2", l.accent2}, {"text", l.text},   {"fg", l.text},
          {"dim", l.dim},       {"faint", l.faint},     {"bg", l.bg},       {"panel", l.bg},
          {"card", l.card},     {"danger", l.danger},   {"ok", l.ok},       {"border", l.card},
          {"warn", *parseHex("#e8be62")},
      };
      if (auto it = named.find(tok); it != named.end()) {
        return hex(it->second, a);
      }
      if (auto c = parseHex(tok)) {
        return hex(*c, a);
      }
      return fallback;
    }

    double alphaOf(const json& tokJson) {
      if (!tokJson.is_string()) return 0.0;
      const std::string tok = tokJson.get<std::string>();
      if (tok.empty() || tok == "transparent" || tok == "none") return 0.0;
      if (const auto slash = tok.rfind('/'); slash != std::string::npos && slash > 0) {
        try {
          return std::stod(tok.substr(slash + 1));
        } catch (...) {
        }
      }
      return 1.0;
    }

    json pairOf(const json& v, double a, double b) {
      if (v.is_array() && v.size() >= 2) return json::array({v[0], v[1]});
      if (v.is_array() && v.size() == 1) return json::array({v[0], v[0]});
      if (v.is_number()) return json::array({v, v});
      return json::array({a, b});
    }

    double jnum(const json& j, const char* key, double fallback) {
      auto it = j.find(key);
      return it != j.end() && it->is_number() ? it->get<double>() : fallback;
    }

    std::string jstr(const json& j, const char* key, const std::string& fallback = "") {
      auto it = j.find(key);
      return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
    }

    // The classic bar, built from the Settings > Bar and Workspaces options.
    json legacyBar(const json& s) {
      const json b = s.value("bar", json::object());
      const json mods = b.value("modules", json::object());
      const std::string style = b.value("style", "islands");
      const bool islands = style == "islands", floating = style == "floating", solid = style == "solid";
      const bool centered = b.value("layout", "classic") == "centered";
      const bool bottom = b.value("position", "top") == "bottom";
      const bool acc = b.value("accentLabels", false);
      const double opacity = b.value("opacity", 0.5);
      const int radius = b.value("radius", 10);
      const int height = b.value("height", 28);
      const bool outline = b.value("outline", false);
      auto lbl = [&](const std::string& t) { return acc ? "<font color=\"accent\">" + t + "</font>" : t; };
      auto mod = [&](const char* key, bool def) { return mods.value(key, def); };
      const std::string scrollStats = b.value("scrollStats", "volume");
      auto stats = [&](json m) {
        if (scrollStats == "volume") {
          m["scrollUp"] = "volume:up";
          m["scrollDown"] = "volume:down";
        } else {
          m["scrollUp"] = "none";
          m["scrollDown"] = "none";
        }
        return m;
      };
      std::ostringstream bg;
      bg << "bg/" << opacity;
      json island = {{"type", "group"},
                     {"bg", islands ? bg.str() : "transparent"},
                     {"border", outline && islands ? "text/0.12" : ""},
                     {"borderWidth", outline && islands ? 1 : 0},
                     {"radius", radius}};
      const int hg = b.value("hoverGrow", true) ? 4 : 0;
      // dwl tags sit flush in the corner on the classic layout.
      const bool flush = s.value("workspaces", json::object()).value("style", "pills") == "dwl" && !centered;
      json wsGroup = island;
      wsGroup["scrollUp"] = "workspace:prev";
      wsGroup["scrollDown"] = "workspace:next";
      wsGroup["gap"] = flush || centered ? json::array({0, 0}) : json::array({6, 0});
      // No inset, flush or not.
      wsGroup["inset"] = json::array({0, 0});
      if (flush) wsGroup["radius"] = 0;
      wsGroup["modules"] = json::array(
          {json{{"type", "workspaces"}, {"padding", flush ? json::array({0, 0}) : json::array({8, 8})}, {"gap", json::array({0, 0})}},
           json{{"type", "title"}, {"show", mod("title", false) && !centered}, {"padding", json::array({12, 10})},
                {"maxLength", b.value("titleWidth", 60)}}}
      );
      json clockGroup = island;
      clockGroup["gap"] = centered ? json::array({6, 0}) : json::array({0, 0});
      const std::string scrollClock = b.value("scrollClock", "volume");
      clockGroup["modules"] = json::array({json{
          {"type", "clock"}, {"timeFormat", b.value("clock", "HH:mm")}, {"bold", b.value("clockBold", true)},
          {"hoverGrow", hg},
          {"scrollUp", scrollClock == "volume" ? "volume:up" : scrollClock == "workspaces" ? "workspace:prev" : "none"},
          {"scrollDown", scrollClock == "volume" ? "volume:down" : scrollClock == "workspaces" ? "workspace:next" : "none"}}});
      json right = json::array();
      if (mod("media", true))
        right.push_back(stats({{"type", "media"}, {"width", b.value("mediaWidth", 20)}, {"hoverGrow", hg},
                               {"popup", b.value("mediaPopup", true)}}));
      right.push_back({{"type", "caffeine"}, {"fg", acc ? "accent" : ""}, {"fontSize", "+2"}, {"padding", json::array({10, 7})}});
      if (mod("cpu", true)) right.push_back(stats({{"type", "cpu"}, {"format", lbl("CPU") + " {usage}%"}, {"hoverGrow", hg}}));
      if (mod("ram", true)) right.push_back(stats({{"type", "ram"}, {"format", lbl("RAM") + " {percent}%"}, {"hoverGrow", hg}}));
      if (mod("gpu", false)) right.push_back(stats({{"type", "gpu"}, {"format", lbl("GPU") + " {usage}%"}, {"hoverGrow", hg}}));
      if (mod("temp", false)) right.push_back(stats({{"type", "temp"}, {"hoverGrow", hg}}));
      if (mod("volume", true))
        right.push_back({{"type", "volume"}, {"fontSize", "+2"}, {"padding", json::array({10, 0})}, {"gap", json::array({2, 0})}, {"hoverGrow", hg}});
      if (mod("network", true))
        right.push_back(stats({{"type", "network"}, {"fg", acc ? "accent" : ""}, {"fontSize", "+2"}, {"padding", json::array({10, 7})}, {"hoverGrow", hg}}));
      if (mod("tray", true))
        right.push_back({{"type", "tray"}, {"padding", json::array({10, 6})}, {"gap", json::array({0, 0})}, {"iconSize", b.value("trayIconSize", 14)}});
      if (mod("power", true))
        right.push_back(stats({{"type", "power"}, {"fg", acc ? "accent" : ""}, {"fontSize", "+4"}, {"padding", json::array({10, 6})}, {"gap", json::array({2, 10})}}));
      json rightGroup = island;
      rightGroup["gap"] = json::array({0, 6});
      rightGroup["modules"] = right;
      json bar = {{"position", bottom ? "bottom" : "top"},
                  {"size", floating ? height - 4 : height},
                  {"margin", floating ? json::array({3, 1, 6}) : json::array({0, 0, 0})},
                  {"bg", solid || floating ? bg.str() : "transparent"},
                  {"radius", floating ? radius : 0},
                  {"exclusive", true},
                  {"group", {{"inset", floating ? json::array({1, 1}) : json::array({4, 2})}}}};
      bar["start"] = json::array({centered ? clockGroup : wsGroup});
      bar["center"] = json::array({centered ? wsGroup : clockGroup});
      bar["end"] = json::array({rightGroup});
      return bar;
    }

    // bar.volumeStep and brightness.step: the step for volume, mic and brightness actions when a module has
    // no "step" of its own. Set by translateBars.
    double g_volumeStep = 5.0;
    double g_brightnessStep = 5.0;

    // Translates a Kusanagi action into the config's action grammar (an IPC command, "exec ..." or "none").
    // `step` is the module's own step, or 0 for the default.
    std::string translateAction(const json& a, double step = 0.0) {
      if (a.is_array()) {
        std::string cmd;
        for (const auto& part : a) {
          if (part.is_string()) cmd += (cmd.empty() ? "" : " ") + part.get<std::string>();
        }
        return cmd.empty() ? "none" : "exec " + cmd;
      }
      if (!a.is_string()) return "none";
      const std::string s = a.get<std::string>();
      if (s.empty() || s == "none" || s == "alt") return "none";
      const auto colon = s.find(':');
      const std::string name = colon == std::string::npos ? s : s.substr(0, colon);
      const std::string arg = colon == std::string::npos ? "" : s.substr(colon + 1);
      static const std::map<std::string, std::string> tabs{
          {"home", "home"}, {"sound", "audio"}, {"network", "network"}, {"bluetooth", "bluetooth"},
          {"system", "system"}, {"inbox", "notifications"}, {"quick", "home"}
      };
      if (name == "panel") {
        auto it = tabs.find(arg);
        return it != tabs.end() ? "panel-toggle control-center " + it->second : "panel-toggle control-center";
      }
      if (name == "launcher") return "panel-toggle launcher";
      if (name == "settings") return "settings-toggle";
      if (name == "power") return "panel-toggle session";
      if (name == "wallpaper") return "panel-toggle wallpaper";
      if (name == "clipboard") return "panel-toggle clipboard";
      if (name == "lock") return "session lock";
      if (name == "notifs") return "panel-toggle control-center notifications";
      if (name == "dnd") return "notification-dnd-toggle";
      if (name == "caffeine") return "caffeine-toggle";
      if (name == "media") {
        if (arg == "next") return "media next";
        if (arg == "prev") return "media previous";
        if (arg == "popup") return "panel-toggle control-center home";
        return "media toggle";
      }
      auto pct = [](double v) {
        std::ostringstream o;
        o << v << "%";
        return o.str();
      };
      if (name == "volume") return arg == "mute" ? std::string("volume-mute") : (arg == "down" ? "volume-down " : "volume-up ") + pct(step > 0 ? step : g_volumeStep);
      if (name == "mic") return arg == "mute" ? std::string("mic-mute") : (arg == "down" ? "mic-volume-down " : "mic-volume-up ") + pct(step > 0 ? step : g_volumeStep);
      if (name == "workspace") return arg == "next" ? "workspace-switch next" : "workspace-switch prev";
      // Brightness actions change every screen.
      if (name == "brightness") return (arg == "down" ? "brightness-down all " : "brightness-up all ") + pct(step > 0 ? step : g_brightnessStep);
      if (name == "bluetooth") return arg == "on" ? "bluetooth-enable" : arg == "off" ? "bluetooth-disable" : "bluetooth-toggle";
      if (name == "gamemode") return "exec kusanagi msg gamemode toggle";
      if (name == "record") return "exec kusanagi record " + (arg.empty() ? std::string("replay") : arg);
      // Runs through the shell (kusanagi_ipc.cpp) so the module can show that a check is in progress.
      if (name == "updates") return arg == "check" ? "updates check" : "updates upgrade";
      if (name == "preset") return "exec kusanagi preset " + (arg.empty() ? std::string("next") : arg);
      if (name == "setup") return "exec kusanagi setup";
      return "exec " + s;
    }

    // Default click and scroll actions for each module type.
    json defaultActions(const std::string& type) {
      static const std::map<std::string, json> d{
          {"clock", {{"click", "panel"}}},
          {"volume", {{"click", "panel:sound"}, {"rightClick", "volume:mute"}, {"middleClick", "settings:sound"}, {"scrollUp", "volume:up"}, {"scrollDown", "volume:down"}}},
          {"mic", {{"click", "mic:mute"}, {"scrollUp", "mic:up"}, {"scrollDown", "mic:down"}}},
          {"media", {{"click", "media:toggle"}, {"rightClick", "media:next"}, {"middleClick", "media:prev"}}},
          {"network", {{"click", "panel:network"}, {"middleClick", "settings:network"}}},
          {"notifications", {{"click", "notifs"}, {"rightClick", "dnd"}}},
          {"caffeine", {{"click", "caffeine"}}},
          {"gamemode", {{"click", "gamemode"}}},
          {"launcher", {{"click", "launcher"}, {"rightClick", "settings"}}},
          {"power", {{"click", "power"}}},
          {"brightness", {{"click", "panel:home"}, {"middleClick", "settings:display"}, {"scrollUp", "brightness:up"}, {"scrollDown", "brightness:down"}}},
          {"bluetooth", {{"click", "panel:bluetooth"}, {"rightClick", "bluetooth:toggle"}, {"middleClick", "settings:bluetooth"}}},
          {"weather", {{"click", "panel"}}},
          {"recorder", {{"click", "record:smart"}, {"rightClick", "record:stop"}}},
          {"updates", {{"click", "updates:upgrade"}, {"rightClick", "updates:check"}}},
          {"workspaces", {{"scrollUp", "workspace:prev"}, {"scrollDown", "workspace:next"}}},
      };
      auto it = d.find(type);
      return it != d.end() ? it->second : json::object();
    }

    void translateBars(const json& s, const Look& look, toml::table& t) {
      json bars = s.contains("bars") && s["bars"].is_array() && !s["bars"].empty() ? s["bars"] : json::array({legacyBar(s)});
      const json barDefaults = {{"position", "top"}, {"size", 28}, {"length", 0}, {"margin", json::array({0, 0, 0})},
                                {"padding", 0}, {"spacing", 0}, {"bg", "transparent"}, {"border", ""}, {"borderWidth", 0},
                                {"radius", 0}, {"exclusive", true}, {"layer", "top"}, {"autohide", false}, {"fg", "text"}};
      auto& barTbl = tbl(t, "bar");
      auto& widgetTbl = tbl(t, "widget");
      const std::string clock = s.value("bar", json::object()).value("clock", "HH:mm");
      {
        const json bar = s.value("bar", json::object()), br = s.value("brightness", json::object());
        g_volumeStep = bar.is_object() && bar.value("volumeStep", json()).is_number() ? bar["volumeStep"].get<double>() : 5.0;
        g_brightnessStep = br.is_object() && br.value("step", json()).is_number() ? br["step"].get<double>() : 5.0;
      }
      const double barFontSize = s.value("bar", json::object()).value("fontSize", 11);

      // Looks the bar draws itself with KusanagiBox: each bar, each group and island bars' sections.
      std::map<std::string, json> looks;
      const json groupBase = {{"bg", "transparent"}, {"border", ""}, {"borderWidth", 0}, {"radius", 0},
                              {"padding", json::array({0, 0})}, {"gap", json::array({0, 0})}, {"inset", json::array({0, 0})},
                              {"spacing", 0}, {"capStart", "none"}, {"capEnd", "none"}, {"opacity", 1}, {"line", nullptr}};
      auto lookOf = [](const json& from, std::initializer_list<const char*> keys) {
        json o = json::object();
        for (const char* k : keys)
          if (from.contains(k)) o[k] = from[k];
        return o;
      };

      int bi = 0;
      for (const auto& rawBar : bars) {
        if (!rawBar.is_object()) continue;
        json b = barDefaults;
        for (auto it = rawBar.begin(); it != rawBar.end(); ++it) b[it.key()] = it.value();
        const std::string name = "k" + std::to_string(bi++);
        auto& bt = tbl(barTbl, name);
        const json margin = b["margin"].is_array() ? b["margin"] : json::array({b["margin"], b["margin"], b["margin"]});
        auto m = [&](std::size_t i) { return margin.size() > i && margin[i].is_number() ? margin[i].get<double>() : 0.0; };
        const bool sized = !(b["length"].is_number() && b["length"].get<double>() == 0.0); // "auto", a fraction or px
        bt.insert_or_assign("enabled", true);
        bt.insert_or_assign("position", jstr(b, "position", "top"));
        bt.insert_or_assign("thickness", static_cast<std::int64_t>(jnum(b, "size", 28)));
        bt.insert_or_assign("margin_edge", static_cast<std::int64_t>(m(0)));
        bt.insert_or_assign("margin_opposite_edge", static_cast<std::int64_t>(margin.size() >= 3 ? m(1) : 0));
        bt.insert_or_assign("margin_ends", static_cast<std::int64_t>(margin.size() >= 3 ? m(2) : m(1)));
        bt.insert_or_assign("padding", static_cast<std::int64_t>(jnum(b, "padding", 0)));
        bt.insert_or_assign("widget_spacing", static_cast<std::int64_t>(jnum(b, "spacing", 0)));
        bt.insert_or_assign("radius", static_cast<std::int64_t>(b["radius"].is_number() ? b["radius"].get<double>() : 0));
        // A content-sized (island) bar is clear itself; its look goes on the section capsules.
        bt.insert_or_assign("background_opacity", sized ? 0.0 : alphaOf(b["bg"]));
        bt.insert_or_assign("compositor_blur", false);
        bt.insert_or_assign("shadow", b.value("shadow", false) == true);
        bt.insert_or_assign("auto_hide", b.value("autohide", false) == true);
        const json excl = b["exclusive"];
        bt.insert_or_assign("reserve_space", excl.is_boolean() ? excl.get<bool>() : excl.is_number() ? excl.get<double>() > 0 : true);
        if (const std::string layer = jstr(b, "layer", "top"); layer == "overlay" || layer == "top") bt.insert_or_assign("layer", layer);
        if (!sized && jnum(b, "borderWidth", 0) > 0 && !jstr(b, "border").empty()) {
          bt.insert_or_assign("border", tokenHex(b["border"], look));
          bt.insert_or_assign("border_width", jnum(b, "borderWidth", 1));
        }
        if (!jstr(b, "font").empty()) bt.insert_or_assign("font_family", jstr(b, "font"));
        bt.insert_or_assign("capsule", false);
        // Clicks outside the modules do nothing (the default would open the control centre on right-click).
        {
          toml::table deadZoneActions;
          for (const char* g : {"left", "right", "middle", "scroll_up", "scroll_down"}) deadZoneActions.insert_or_assign(g, "none");
          tbl(bt, "dead_zone").insert_or_assign("actions", std::move(deadZoneActions));
        }
        bt.insert_or_assign("hover_highlight", false); // Modules show hover through their own hoverBg.

        const json groupDefaults = b.value("group", json::object());
        const std::string edge = jstr(b, "position", "top");
        // Matches the classic look: each section measures one spacing past its last item.
        const double spacing = jnum(b, "spacing", 0);
        // The bar's own look. An island bar's look goes on its section capsules instead.
        looks[name] = json{{"_island", true}};
        if (!sized) {
          looks[name] = lookOf(b, {"bg", "border", "borderWidth", "radius", "line"});
          bt.insert_or_assign("background_opacity", 0.0);
          bt.erase("border");
        }
        looks[name]["_spacing"] = spacing;
        const json moduleDefaults = b.value("module", json::object());
        toml::array groups;
        for (const char* section : {"start", "center", "end"}) {
          toml::array lane;
          const json entries = b.value(section, json::array());
          int gi = 0;
          // On an island bar every section sits in one capsule with the bar's look.
          toml::array sectionMembers;
          for (const auto& rawEntry : entries) {
            json entry = rawEntry.is_string() ? json{{"type", rawEntry}} : rawEntry;
            if (!entry.is_object()) continue;
            const bool isGroup = jstr(entry, "type") == "group";
            json group = groupDefaults;
            if (isGroup) for (auto it = entry.begin(); it != entry.end(); ++it) group[it.key()] = it.value();
            // A group's look and geometry. A bare module sits in an invisible group of one whose inset is its
            // groupInset or the bar's group inset.
            json norm = groupBase;
            for (auto it = group.begin(); it != group.end(); ++it)
              if (it.key() != "modules" && it.key() != "module") norm[it.key()] = it.value();
            for (const char* k : {"padding", "gap", "inset"}) norm[k] = pairOf(norm[k], 0, 0);
            const json groupInset = isGroup ? norm["inset"]
                                            : pairOf(entry.contains("groupInset") ? entry["groupInset"] : groupDefaults.value("inset", json()), 0, 0);
            json moduleDefs = moduleDefaults;
            if (isGroup && entry.contains("module") && entry["module"].is_object())
              for (auto it = entry["module"].begin(); it != entry["module"].end(); ++it) moduleDefs[it.key()] = it.value();
            const json modules = isGroup ? entry.value("modules", json::array()) : json::array({entry});
            toml::array members;
            int mi = 0;
            for (const auto& rawModule : modules) {
              json mod = rawModule.is_string() ? json{{"type", rawModule}} : rawModule;
              if (!mod.is_object()) continue;
              json spec = moduleDefs;
              for (auto it = mod.begin(); it != mod.end(); ++it) spec[it.key()] = it.value();
              if (spec.contains("formatAlt")) {
                spec["when"]["alt"]["format"] = spec["formatAlt"];
              }
              const std::string type = jstr(spec, "type", "text");
              if (spec.value("show", true) == false) continue;
              spec["_groupInset"] = groupInset;
              spec["_edge"] = edge;
              // Pointer cursor only when the module or its type has a click action.
              spec["_pointer"] = spec.contains("click") || defaultActions(type).contains("click");
              const double step = spec.value("step", json()).is_number() ? spec["step"].get<double>() : 0.0;
              // A `when` state can bring its own click and scroll actions. The widget picks
              // _stateActions[state][gesture] while in that state. _stateBase is what a gesture does outside the
              // states when only a state gives it an action; it stays bound so the module takes the button.
              json stateBase = json::object();
              if (spec.contains("when") && spec["when"].is_object()) {
                json stateActions = json::object();
                for (auto st = spec["when"].begin(); st != spec["when"].end(); ++st) {
                  if (!st->is_object()) continue;
                  for (const auto& [k, g] : {std::pair{"click", "left"}, {"rightClick", "right"}, {"middleClick", "middle"},
                                             {"scrollUp", "scroll_up"}, {"scrollDown", "scroll_down"}}) {
                    if (!st->contains(k)) continue;
                    const json& a = (*st)[k];
                    stateActions[st.key()][g] = a.is_string() && a.get<std::string>() == "alt" ? std::string("alt") : translateAction(a, step);
                    const json base = spec.contains(k) ? spec[k] : defaultActions(type).value(k, isGroup ? group.value(k, json()) : json());
                    if (base.is_null() || (base.is_string() && (base.get<std::string>() == "none" || base.get<std::string>().empty() || base.get<std::string>() == "alt")))
                      stateBase[g] = base.is_string() && base.get<std::string>() == "alt" ? "alt" : "none";
                  }
                }
                if (!stateActions.empty()) {
                  spec["_stateActions"] = std::move(stateActions);
                  if (!stateBase.empty()) spec["_stateBase"] = stateBase;
                }
              }
              const std::string wname = name + "_" + section + "_" + std::to_string(gi) + "_" + std::to_string(mi++);
              auto& wt = tbl(widgetTbl, wname);
              if (type == "workspaces") {
                spec["_barFontSize"] = b["fontSize"].is_number() && b["fontSize"].get<double>() > 0 ? b["fontSize"].get<double>() : barFontSize;
                wt.insert_or_assign("type", "kusanagi_workspaces");
                wt.insert_or_assign("spec", spec.dump());
              } else if (type == "tray") {
                // The tray widget, laid out and styled from the spec (TrayWidget::Options::kusanagiSpec).
                wt.insert_or_assign("type", "tray");
                wt.insert_or_assign("spec", spec.dump());
              } else {
                // The taskbar reads the dock's pinned, grouped and indicator options.
                if (type == "taskbar") {
                  spec["_dock"] = s.value("dock", json::object());
                  for (const auto& [k, g] : {std::pair{"click", "left"}, {"rightClick", "right"}, {"middleClick", "middle"}})
                    if (isGroup && group.contains(k) && !group[k].is_null()) spec["_groupActions"][g] = translateAction(group[k]);
                }
                spec["_barFg"] = jstr(b, "fg", "text");
                spec["_barFont"] = jstr(b, "font");
                spec["_barFontSize"] = b["fontSize"].is_number() && b["fontSize"].get<double>() > 0 ? b["fontSize"].get<double>() : barFontSize;
                spec["_clock"] = clock;
                spec["padding"] = pairOf(spec.value("padding", json()), 10, 10);
                wt.insert_or_assign("type", "kusanagi");
                wt.insert_or_assign("spec", spec.dump());
              }
              // A module with its own background or border gets a widget capsule, except Kusanagi widgets,
              // which draw theirs with KusanagiBox (gradients, caps, lines).
              if (type != "workspaces" && type != "tray" && wt["type"].value_or(std::string()) != "kusanagi"
                  && (alphaOf(spec.value("bg", json())) > 0 || (jnum(spec, "borderWidth", 0) > 0 && !jstr(spec, "border").empty()))) {
                wt.insert_or_assign("capsule", true);
                wt.insert_or_assign("capsule_fill", tokenHex(spec.value("bg", json("transparent")), look, "#00000000"));
                wt.insert_or_assign("capsule_radius", jnum(spec, "radius", 0));
                wt.insert_or_assign("capsule_padding", 0.0);
                if (jnum(spec, "borderWidth", 0) > 0 && !jstr(spec, "border").empty()) {
                  wt.insert_or_assign("capsule_border", tokenHex(spec["border"], look));
                  wt.insert_or_assign("capsule_border_width", jnum(spec, "borderWidth", 1));
                }
              }
              // Actions come from the module's spec (module defaults included), then its type's defaults, then
              // its group, so a press the module doesn't take falls through to the group.
              const json defs = defaultActions(type);
              toml::table actions;
              const std::pair<const char*, const char*> gestures[] = {
                  {"click", "left"}, {"rightClick", "right"}, {"middleClick", "middle"}, {"scrollUp", "scroll_up"}, {"scrollDown", "scroll_down"}};
              // Taskbar items sit above their group; a click between them reaches the group via _groupActions.
              const bool items = type == "taskbar";
              for (const auto& [k, g] : gestures) {
                const bool button = std::string_view(k).find("scroll") == std::string_view::npos;
                json a = spec.contains(k) ? spec[k] : defs.contains(k) ? defs[k] : isGroup && group.contains(k) && !(items && button) ? group[k] : json();
                if (!a.is_null()) actions.insert_or_assign(g, translateAction(a, step));
                // Bound only so the module takes the button; the widget decides what runs (see _stateBase).
                if (stateBase.contains(g)) actions.insert_or_assign(g, "exec true");
              }
              // Without a middle-click action nothing happens, instead of the default "open this widget's
              // settings" default. Taskbar items handle middle-click themselves.
              if (!actions.contains("middle")) actions.insert_or_assign("middle", "none");
              // Every wheel step runs the action, including cycling ones like workspace-switch.
              wt.insert_or_assign("scroll_repeat", "steps");
              if (!actions.empty()) wt.insert_or_assign("actions", std::move(actions));
              members.push_back(wname);
              sectionMembers.push_back(wname);
            }
            // A group becomes a capsule group around its members, and the lane names the group. The bar lays
            // it out as gap, cap, padding, modules, padding, cap, gap, with the inset across.
            const bool capsule = isGroup && !members.empty();
            if (capsule) {
              // Clicks and scrolls on the group itself: its padding, caps and the space between modules.
              json groupActions = json::object();
              for (const auto& [k, g] : {std::pair{"click", "left"}, {"rightClick", "right"}, {"middleClick", "middle"},
                                         {"scrollUp", "scroll_up"}, {"scrollDown", "scroll_down"}})
                if (group.contains(k) && !group[k].is_null()) groupActions[g] = translateAction(group[k]);
              norm["_actions"] = std::move(groupActions);
              looks[name + "_" + section + "_" + std::to_string(gi)] = norm;
            }
            if (capsule) {
              lane.push_back(makeCapsuleGroupToken(name + "_" + section + "_" + std::to_string(gi)));
            } else {
              for (const auto& w : members) lane.push_back(*w.value<std::string>());
            }
            if (capsule) {
              toml::table g;
              g.insert_or_assign("id", name + "_" + section + "_" + std::to_string(gi));
              g.insert_or_assign("members", members);
              g.insert_or_assign("fill", tokenHex(group.value("bg", json("transparent")), look, "#00000000"));
              g.insert_or_assign("radius", jnum(group, "radius", 0));
              g.insert_or_assign("padding", 0.0);
              g.insert_or_assign("widget_spacing", static_cast<std::int64_t>(jnum(group, "spacing", 0)));
              if (jnum(group, "borderWidth", 0) > 0 && !jstr(group, "border").empty()) {
                g.insert_or_assign("border", tokenHex(group["border"], look));
                g.insert_or_assign("border_width", jnum(group, "borderWidth", 1));
              }
              groups.push_back(std::move(g));
            }
            ++gi;
          }
          if (sized && !sectionMembers.empty() && alphaOf(b["bg"]) > 0) {
            json sectionLook = lookOf(b, {"bg", "border", "borderWidth", "radius", "line"});
            sectionLook["padding"] = json::array({jnum(b, "padding", 0), jnum(b, "padding", 0)});
            looks[name + "_" + section] = std::move(sectionLook);
            toml::table g;
            g.insert_or_assign("id", name + "_" + section);
            g.insert_or_assign("members", sectionMembers);
            g.insert_or_assign("fill", tokenHex(b["bg"], look, "#00000000"));
            g.insert_or_assign("radius", b["radius"].is_number() ? b["radius"].get<double>() : 0.0);
            g.insert_or_assign("padding", jnum(b, "padding", 0));
            g.insert_or_assign("widget_spacing", static_cast<std::int64_t>(jnum(b, "spacing", 0)));
            if (jnum(b, "borderWidth", 0) > 0 && !jstr(b, "border").empty()) {
              g.insert_or_assign("border", tokenHex(b["border"], look));
              g.insert_or_assign("border_width", jnum(b, "borderWidth", 1));
            }
            groups.push_back(std::move(g));
            lane = toml::array{makeCapsuleGroupToken(name + "_" + section)};
          }
          bt.insert_or_assign(section, std::move(lane));
        }
        if (!groups.empty()) bt.insert_or_assign("capsule_group", std::move(groups));
      }
      kusanagi_bar::setLooks(std::move(looks));
    }

  } // namespace

  bool isKusanagiSourceFile(std::string_view name) {
    return name == "settings.json" || name == "colors.json" || name == "wallpaper";
  }

  KusanagiImport importKusanagiSettings(const std::filesystem::path& configDir) {
    KusanagiImport out;
    const auto settingsPath = configDir / "settings.json";
    auto settingsOpt = readJson(settingsPath);
    if (!settingsOpt) {
      return out;
    }
    const json& s = *settingsOpt;
    kusanagi::setSettings(s); // Native surfaces read Kusanagi-only options from here.
    out.files.push_back(settingsPath);
    const auto colorsPath = configDir / "colors.json";
    auto colors = readJson(colorsPath);
    if (colors) {
      out.files.push_back(colorsPath);
    }

    toml::table& t = out.table;

    // Look
    const Look look = resolveLook(s, colors);
    writePalette(configDir, look);
    {
      auto& theme = tbl(t, "theme");
      theme.insert_or_assign("source", "custom");
      theme.insert_or_assign("custom_palette", "kusanagi");
      // Kusanagi themes other apps through its own wallpaper hook, so the built-in templates stay off.
      auto& templates = tbl(theme, "templates");
      templates.insert_or_assign("enable_builtin_templates", false);
      templates.insert_or_assign("enable_community_templates", false);
      theme.insert_or_assign("mode", get<std::string>(s, "look", "palette", "") == "catppuccin-latte" ? "light" : "dark");
    }
    {
      auto& shell = tbl(t, "shell");
      shell.insert_or_assign("font_family", get<std::string>(s, "look", "font", "JetBrainsMono Nerd Font"));
      shell.insert_or_assign("corner_radius_scale", get<double>(s, "look", "radius", 16.0) / 12.0);
      shell.insert_or_assign("polkit_agent", get<bool>(s, "polkit", "enabled", true));
      shell.insert_or_assign("setup_wizard_enabled", false); // Kusanagi has its own first-run setup.
      // Kusanagi's clipboard history is cliphist, on disk. The built-in history keeps every copied
      // payload in RAM (about 80 MB after copying a few images) and re-owns selections of closed apps.
      // Copy and paste in the shell's own fields needs neither.
      shell.insert_or_assign("clipboard_enabled", false);
      shell.insert_or_assign("clipboard_keep_from_closed_apps", false);

      // Kusanagi multiplies durations by animSpeed (0 = off); the config divides by speed.
      const double animSpeed = get<double>(s, "look", "animSpeed", 1.0);
      auto& anim = tbl(shell, "animation");
      anim.insert_or_assign("enabled", animSpeed > 0.0);
      anim.insert_or_assign("speed", animSpeed > 0.0 ? 1.0 / animSpeed : 1.0);

      const bool borders = get<bool>(s, "look", "borders", true);
      auto& panel = tbl(shell, "panel");
      panel.insert_or_assign("borders", borders);
      panel.insert_or_assign("border_width", borders ? 1.0 : 0.0);
      // Panel border: the accent at 0.55 alpha, or the text colour at 0.08.
      panel.insert_or_assign(
          "border_color",
          get<bool>(s, "look", "borderAccent", false) ? hex(look.accent, 0.55) : hex(look.text, 0.08)
      );
      panel.insert_or_assign("shadow", get<bool>(s, "look", "shadows", true));

      auto& launcher = tbl(shell, "launcher");
      launcher.insert_or_assign("sort_by_usage", get<bool>(s, "launcher", "sortByUsage", true));
      launcher.insert_or_assign("app_grid", get<std::string>(s, "launcher", "layout", "list") == "grid");
      // The launcher's own prefixes are single characters (= > : / ?). The other providers stay
      // reachable as //win, //emo, //calc and so on. Enter on a calculator or symbol result only copies.
      launcher.insert_or_assign("provider_prefix", "//");
      launcher.insert_or_assign("auto_paste", "off");
      launcher.insert_or_assign("fetch_exchange_rates", false);
      panel.insert_or_assign(
          "launcher_position", get<std::string>(s, "launcher", "position", "upper") == "center" ? "center" : "top_center"
      );
    }
    {
      auto& shadow = tbl(tbl(t, "shell"), "shadow");
      shadow.insert_or_assign("alpha", get<bool>(s, "look", "shadows", true) ? 0.45 : 0.0);
    }

    // OSD and notifications
    {
      auto& osd = tbl(t, "osd");
      const std::string pos = get<std::string>(s, "osd", "position", "top");
      osd.insert_or_assign("position", osdPosition(pos));
      osd.insert_or_assign("position_vertical", osdPosition(pos));
      osd.insert_or_assign("orientation", pos == "left" || pos == "right" ? "vertical" : "horizontal");
      osd.insert_or_assign("hide_delay_ms", static_cast<std::int64_t>(get<int>(s, "osd", "timeout", 1400)));
      osd.insert_or_assign("border", get<bool>(s, "look", "borders", true));
      osd.insert_or_assign("border_color", hex(look.text, 0.08));
      auto& kinds = tbl(osd, "kinds");
      kinds.insert_or_assign("volume", get<bool>(s, "osd", "volume", true));
      kinds.insert_or_assign("volume_input", get<bool>(s, "osd", "mic", true));
      kinds.insert_or_assign("brightness", get<bool>(s, "osd", "brightness", true));
      // OSDs Kusanagi doesn't have. The lock keys one would also poll.
      for (const char* k : {"lock_keys", "keyboard_layout", "privacy", "power_profile", "nightlight", "dnd", "wifi", "bluetooth", "caffeine"}) {
        kinds.insert_or_assign(k, false);
      }
    }
    {
      auto& n = tbl(t, "notification");
      n.insert_or_assign("position", notificationPosition(get<std::string>(s, "notifications", "position", "top-right")));
      n.insert_or_assign("border", get<bool>(s, "look", "borders", true));
      n.insert_or_assign("border_color", hex(look.text, 0.08));
    }

    // Wallpaper
    {
      auto& w = tbl(t, "wallpaper");
      w.insert_or_assign("enabled", get<std::string>(s, "wallpaper", "renderer", "kusanagi") == "kusanagi");
      w.insert_or_assign("directory", expandHome(get<std::string>(s, "wallpaper", "folder", "~/Pictures/Wallpapers")));
      static const std::map<std::string, std::string> fills{
          {"fill", "crop"}, {"fit", "fit"}, {"stretch", "stretch"}, {"center", "center"}, {"tile", "repeat"}
      };
      const auto fill = fills.find(get<std::string>(s, "wallpaper", "fill", "fill"));
      w.insert_or_assign("fill_mode", fill != fills.end() ? fill->second : "crop");
      // Closest engine transition for each Kusanagi one.
      static const std::map<std::string, std::string> trans{
          {"fade", "fade"}, {"blur", "fade"}, {"wipe", "wipe"}, {"grow", "disc"},
          {"slide", "wipe"}, {"zoom", "zoom"}, {"blinds", "stripes"}
      };
      const std::string tr = get<std::string>(s, "wallpaper", "transition", "random");
      toml::array list;
      if (auto it = trans.find(tr); it != trans.end()) {
        list.push_back(it->second);
      } else {
        for (const char* x : {"fade", "wipe", "disc", "stripes", "zoom"}) {
          list.push_back(x);
        }
      }
      w.insert_or_assign("transition", std::move(list));
      w.insert_or_assign("transition_duration", static_cast<std::int64_t>(get<int>(s, "wallpaper", "duration", 1100)));
      const auto wallFile = configDir / "wallpaper";
      if (const std::string current = readLine(wallFile); !current.empty()) {
        tbl(w, "default").insert_or_assign("path", current);
        out.files.push_back(wallFile);
      }
      const int slideshow = get<int>(s, "wallpaper", "slideshow", 0);
      auto& autom = tbl(w, "automation");
      autom.insert_or_assign("enabled", slideshow > 0);
      autom.insert_or_assign("interval_seconds", static_cast<std::int64_t>(std::max(1, slideshow) * 60));
      autom.insert_or_assign("order", "random");
    }

    // Lock screen. The look itself is in shell/lockscreen/kusanagi_lock_view.
    {
      auto& l = tbl(t, "lockscreen");
      l.insert_or_assign("transition", toml::array{}); // The Kusanagi lock does its own fade and zoom.
      l.insert_or_assign("blurred_desktop", false);
      l.insert_or_assign("fingerprint", false);        // Fingerprint auth is left to the PAM stack.
      // lock.blur (0..1) is scaled so the lock screen blur (radius = intensity * 40) matches the classic lock.
      l.insert_or_assign("blur_intensity", std::clamp(get<double>(s, "lock", "blur", 0.55) * kLockBlurScale, 0.0, 1.0));
      l.insert_or_assign("tint_intensity", 0.0);       // lock.dim darkens with black instead.
    }

    // Idle. Kusanagi times are in minutes, 0 means never.
    {
      const bool on = get<bool>(s, "idle", "enabled", false);
      auto& behaviors = tbl(tbl(t, "idle"), "behavior");
      auto add = [&](const char* name, const char* action, int minutes) {
        auto& b = tbl(behaviors, name);
        b.insert_or_assign("action", action);
        b.insert_or_assign("timeout", static_cast<std::int64_t>(std::max(1, minutes) * 60));
        b.insert_or_assign("enabled", on && minutes > 0);
      };
      add("lock", "lock", get<int>(s, "idle", "lock", 10));
      add("screen-off", "screen_off", get<int>(s, "idle", "screenOff", 15));
      add("suspend", "suspend", get<int>(s, "idle", "suspend", 0));
    }

    translateBars(s, look, t);

    tbl(t, "brightness").insert_or_assign("enable_ddcutil", get<bool>(s, "brightness", "ddc", true));

    // Weather: weather.location is a place name; empty means "where my IP is".
    {
      std::string place = get<std::string>(s, "weather", "location", "");
      place.erase(0, place.find_first_not_of(" \t"));
      place.erase(place.find_last_not_of(" \t") + 1);
      auto& location = tbl(t, "location");
      location.insert_or_assign("auto_locate", place.empty());
      location.insert_or_assign("address", place);
      tbl(t, "weather").insert_or_assign(
          "unit", get<std::string>(s, "weather", "units", "metric") == "imperial" ? "imperial" : "metric"
      );
    }

    // Features Kusanagi doesn't use.
    tbl(t, "dock").insert_or_assign("enabled", false);
    tbl(t, "desktop_widgets").insert_or_assign("enabled", false);
    tbl(t, "backdrop").insert_or_assign("enabled", false);
    // No plugins and no plugin sources.
    tbl(t, "plugins").insert_or_assign("auto_update", "none");

    // KUSANAGI_IMPORT_DUMP=<file> writes the translated table, for debugging.
    if (const char* dump = std::getenv("KUSANAGI_IMPORT_DUMP"); dump != nullptr && dump[0] != '\0') {
      std::ofstream(dump) << out.table << "\n";
    }
    return out;
  }

  bool loadKusanagiLook(const std::filesystem::path& configDir) {
    auto settingsOpt = readJson(configDir / "settings.json");
    if (!settingsOpt) {
      return false;
    }
    kusanagi::setSettings(*settingsOpt);
    const Look l = resolveLook(*settingsOpt, readJson(configDir / "colors.json"));
    // Same roles writePalette() writes.
    auto c = [](const Rgb& v) {
      return rgba(static_cast<float>(v.r), static_cast<float>(v.g), static_cast<float>(v.b), 1.0F);
    };
    setPalette(Palette{
        .primary = c(l.accent),
        .onPrimary = c(l.bg),
        .secondary = c(l.accent2),
        .onSecondary = c(l.bg),
        .tertiary = c(l.ok),
        .onTertiary = c(l.bg),
        .error = c(l.danger),
        .onError = c(l.bg),
        .surface = c(l.bg),
        .onSurface = c(l.text),
        .surfaceVariant = c(l.card),
        .onSurfaceVariant = c(l.dim),
        .outline = c(l.faint),
        .shadow = rgba(0.0F, 0.0F, 0.0F, 1.0F),
        .hover = c(mix(l.card, l.text, 0.06)),
        .onHover = c(l.text),
    });
    return true;
  }

} // namespace kusanagi::config
