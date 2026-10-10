// Custom bar layout editor in Settings > Bar. Edits settings.json "bars" (see docs/bar.md) with a live
// preview, section chips and an inspector for the selected module, plus raw JSON for anything without a
// dedicated control. Every change is written right away and the real bar follows.

#include "render/core/renderer.h"
#include "shell/kusanagi/settings/bar_preview.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_page_bar.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <regex>

namespace kusanagi::sp {

  // Steppers write reals, so turn whole numbers back into integers everywhere in the layout.
  json deepNormalized(const json& v) {
      if (v.is_number_float()) {
        const double d = v.get<double>();
        if (std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 1e15) return json(static_cast<std::int64_t>(d));
        return v;
      }
      if (v.is_array()) {
        json out = json::array();
        for (const auto& e : v) out.push_back(deepNormalized(e));
        return out;
      }
      if (v.is_object()) {
        json out = json::object();
        for (auto it = v.begin(); it != v.end(); ++it) out[it.key()] = deepNormalized(it.value());
        return out;
      }
      return v;
  }

  namespace {

    // A missing key or null.
    bool unset(const json& v) { return v.is_null() || v.is_discarded(); }
    // Values that make setKey remove the key: missing, null or "".
    bool removes(const json& v) { return unset(v) || (v.is_string() && v.get<std::string>().empty()); }
    json norm(const json& e) { return e.is_string() ? json{{"type", e}} : e; }
    json keyOf(const json& o, const std::string& key) {
      if (!o.is_object()) return json(json::value_t::discarded);
      const auto it = o.find(key);
      return it == o.end() ? json(json::value_t::discarded) : *it;
    }

    // Catalogues

    struct TypeInfo {
      const char* t;
      const char* label;
      char32_t icon;
    };
    const std::vector<TypeInfo>& types() {
      static const std::vector<TypeInfo> t{
          {"workspaces", "Workspaces", 0xf0570}, {"title", "Window title", 0xf05b1}, {"taskbar", "Taskbar", 0xf0e2a},
          {"clock", "Clock", 0xf0150},           {"media", "Media", 0xf075a},        {"cpu", "CPU", 0xf0ee0},
          {"ram", "RAM", 0xf035b},               {"gpu", "GPU", 0xf08ae},            {"temp", "Temperature", 0xf050f},
          {"disk", "Disk", 0xf02ca},             {"network", "Network", 0xf0200},    {"volume", "Volume", 0xf057e},
          {"mic", "Microphone", 0xf036c},        {"battery", "Battery", 0xf0079},    {"brightness", "Brightness", 0xf00df},
          {"bluetooth", "Bluetooth", 0xf00af},   {"tray", "Tray", 0xf003b},          {"notifications", "Notifications", 0xf009a},
          {"weather", "Weather", 0xf0590},       {"uptime", "Uptime", 0xf0954},      {"caffeine", "Caffeine", 0xf0176},
          {"gamemode", "Game mode", 0xf0297},    {"recorder", "Recorder", 0xf044a},  {"updates", "Updates", 0xf06b0},
          {"launcher", "Launcher", 0xf003b},     {"power", "Power", 0xf0425},        {"text", "Text", 0xf0284},
          {"sep", "Separator", 0xf01d8},         {"spacer", "Spacer", 0xf0c0b},      {"custom", "Command", 0xf018d},
          {"group", "Group", 0xf0569},
      };
      return t;
    }
    std::pair<std::string, char32_t> typeInfo(const std::string& t) {
      for (const auto& x : types())
        if (t == x.t) return {x.label, x.icon};
      return {t, 0xf0166};
    }
    // What each module's format can use, and its own options beyond the common style keys.
    std::string typeDoc(const std::string& t) {
      static const std::map<std::string, std::string> d{
          {"workspaces", "options: style (pills dots numbers roman kanji custom dwl), icons, glow, colors { active occupied empty urgent onActive }"},
          {"title", "{title} {app} · maxLength"},
          {"taskbar", "options: titles, titleWidth, iconSize, spacing, colors { active hover text }"},
          {"clock", "{time} {date} · timeFormat, dateFormat (Qt: HH mm ss · h AP · ddd dddd · d MMM yyyy)"},
          {"media", "{track} {title} {artist} {album} {player} · width, marquee, popup · states playing paused"},
          {"cpu", "{usage} · level = usage"},
          {"ram", "{percent} {used} {total} {free}"},
          {"gpu", "{usage} {vramUsed} {vramTotal}"},
          {"temp", "{temp} {cpu} {gpu} · sensor cpu|gpu"},
          {"disk", "{percent} {used} {total} {free}"},
          {"network", "{icon} {ifname} {ip} {down} {up} · states wifi ethernet disconnected"},
          {"volume", "{icon} {volume} · step · state muted"},
          {"mic", "{icon} {volume} · state muted"},
          {"brightness", "{icon} {percent} · step · scroll changes every screen (laptop panel, DDC/CI monitors)"},
          {"bluetooth", "{icon} {device} {count} {battery} · states off on connected blocked · right click on/off"},
          {"battery", "{icon} {capacity} {time} · states charging discharging full, warning ≤30 critical ≤15"},
          {"tray", "options: iconSize, spacing"},
          {"notifications", "{icon} {count} · states dnd unread none"},
          {"weather", "{icon} {temp}{unit} {feels} {desc} {place}"},
          {"uptime", "{uptime} {load}"},
          {"caffeine", "always: true shows it while off too · states on off"},
          {"gamemode", "always: true · states on off"},
          {"recorder", "{icon} {time} {mode} {backend} · always: true · states off replay record stream · click: replay → save, else stop/start"},
          {"updates", "{count} · always: true · states none some many unknown · click upgrade, right click check"},
          {"launcher", "a button (click = launcher)"},
          {"power", "a button (click = power menu)"},
          {"text", "text: shown as is (markup ok)"},
          {"sep", "format: the glyph"},
          {"spacer", "size: px"},
          {"custom", "exec (sh -c), interval s, stream true, refresh true, game true · prints text or JSON {text tooltip class percentage}"},
      };
      const auto it = d.find(t);
      return it != d.end() ? it->second : std::string();
    }

    // One editable key.
    struct Desc {
      std::string key;
      std::string label;
      std::string kind; // text, color, int, pair, triple, bool, enum, caps or line.
      json def = json(json::value_t::discarded);
      int from = 0;
      int to = 100;
      int step = 1;
      std::vector<Option> options; // For enum fields.
      std::string placeholder;
      bool mono = false;
    };
    std::vector<Option> strOpts(std::initializer_list<const char*> l) {
      std::vector<Option> out;
      for (const char* s : l) out.push_back({s, s});
      return out;
    }
    const std::vector<Desc>& barFields() {
      static const std::vector<Desc> f{
          {.key = "position", .label = "Edge", .kind = "enum", .options = strOpts({"top", "bottom", "left", "right"})},
          {.key = "size", .label = "Thickness", .kind = "int", .def = 28, .from = 14, .to = 96, .step = 2},
          {.key = "length", .label = "Length", .kind = "enum", .def = 0,
           .options = {{"Full", 0}, {"Fit", "auto"}, {"¾", 0.75}, {"½", 0.5}, {"⅓", 0.33}}},
          {.key = "align", .label = "Align (short bars)", .kind = "enum", .def = "center", .options = strOpts({"start", "center", "end"})},
          {.key = "margin", .label = "Margin edge · inner · sides", .kind = "triple", .from = 0, .to = 60},
          {.key = "padding", .label = "Padding at the ends", .kind = "int", .def = 0, .from = 0, .to = 60},
          {.key = "spacing", .label = "Between entries", .kind = "int", .def = 0, .from = 0, .to = 40},
          {.key = "bg", .label = "Background", .kind = "color"},
          {.key = "radius", .label = "Roundness", .kind = "int", .def = 0, .from = 0, .to = 40},
          {.key = "border", .label = "Border", .kind = "color"},
          {.key = "borderWidth", .label = "Border width", .kind = "int", .def = 0, .from = 0, .to = 6},
          {.key = "line", .label = "Edge line", .kind = "line"},
          {.key = "fg", .label = "Text colour", .kind = "color"},
          {.key = "fontSize", .label = "Text size (0 = Appearance)", .kind = "int", .def = 0, .from = 0, .to = 24},
          {.key = "font", .label = "Font", .kind = "text", .placeholder = "Appearance font"},
          {.key = "exclusive", .label = "Keep windows clear", .kind = "bool", .def = true},
          {.key = "autohide", .label = "Hide until the pointer hits the edge", .kind = "bool", .def = false},
          {.key = "layer", .label = "Layer", .kind = "enum", .def = "top", .options = strOpts({"top", "overlay", "bottom"})},
          {.key = "screen", .label = "Screen (blank = all)", .kind = "text", .placeholder = "DP-1"},
      };
      return f;
    }
    const std::vector<Desc>& styleFields() {
      static const std::vector<Desc> f{
          {.key = "bg", .label = "Background", .kind = "color"},
          {.key = "radius", .label = "Roundness", .kind = "int", .def = 0, .from = 0, .to = 40},
          {.key = "border", .label = "Border", .kind = "color"},
          {.key = "borderWidth", .label = "Border width", .kind = "int", .def = 0, .from = 0, .to = 6},
          {.key = "padding", .label = "Padding before · after", .kind = "pair", .from = 0, .to = 60},
          {.key = "gap", .label = "Margin before · after", .kind = "pair", .from = 0, .to = 60},
          {.key = "inset", .label = "Inset edge · inner", .kind = "pair", .from = 0, .to = 30},
          {.key = "capStart", .label = "Start cap", .kind = "caps"},
          {.key = "capEnd", .label = "End cap", .kind = "caps"},
          {.key = "capStartBg", .label = "Behind the start cap", .kind = "color"},
          {.key = "capEndBg", .label = "Behind the end cap", .kind = "color"},
          {.key = "line", .label = "Indicator line", .kind = "line"},
      };
      return f;
    }
    const std::vector<Desc>& moduleFields() {
      static const std::vector<Desc> f = []() {
        std::vector<Desc> out{
            {.key = "format", .label = "Format", .kind = "text", .placeholder = "default", .mono = true},
            {.key = "formatAlt", .label = "Alt format (click: alt)", .kind = "text", .mono = true},
            {.key = "fg", .label = "Text colour", .kind = "color"},
            {.key = "hoverBg", .label = "Hover background", .kind = "color"},
            {.key = "hoverFg", .label = "Hover text colour", .kind = "color"},
            {.key = "fontSize", .label = "Text size (+2 / 14)", .kind = "text", .placeholder = "bar's"},
            {.key = "bold", .label = "Bold", .kind = "bool", .def = false},
            {.key = "hoverGrow", .label = "Grow on hover (px)", .kind = "int", .def = 0, .from = 0, .to = 8},
            {.key = "rotate", .label = "Turn text (side bars)", .kind = "bool", .def = false},
        };
        for (const auto& d : styleFields()) out.push_back(d);
        for (const auto& [k, l] : std::vector<std::pair<const char*, const char*>>{
                 {"click", "Click"}, {"rightClick", "Right click"}, {"middleClick", "Middle click"}, {"scrollUp", "Scroll up"}, {"scrollDown", "Scroll down"}})
          out.push_back({.key = k, .label = l, .kind = "text", .placeholder = "default", .mono = true});
        out.push_back({.key = "tooltip", .label = "Tooltip", .kind = "text", .placeholder = "default"});
        return out;
      }();
      return f;
    }
    const std::vector<Desc>& groupFields() {
      static const std::vector<Desc> f = []() {
        std::vector<Desc> out = styleFields();
        out.push_back({.key = "spacing", .label = "Between modules", .kind = "int", .def = 0, .from = 0, .to = 30});
        out.push_back({.key = "click", .label = "Click", .kind = "text", .mono = true});
        out.push_back({.key = "scrollUp", .label = "Scroll up", .kind = "text", .mono = true});
        out.push_back({.key = "scrollDown", .label = "Scroll down", .kind = "text", .mono = true});
        return out;
      }();
      return f;
    }
    struct FoldPlan {
      const char* title;
      const char* hint;
      bool open;
      std::vector<std::string> keys;
    };
    const std::vector<FoldPlan>& modulePlan() {
      static const std::vector<FoldPlan> p{
          {"Text", "what it says and how", true, {"format", "formatAlt", "fg", "fontSize", "bold", "hoverFg", "hoverGrow", "rotate", "tooltip"}},
          {"Background & shape", "colour, corners, border, powerline caps, underline", true,
           {"bg", "hoverBg", "radius", "border", "borderWidth", "capStart", "capEnd", "capStartBg", "capEndBg", "line"}},
          {"Spacing", "room inside, outside and across", false, {"padding", "gap", "inset", "spacing"}},
          {"Clicks & scrolling", "built-in actions or any shell command", false, {"click", "rightClick", "middleClick", "scrollUp", "scrollDown"}},
      };
      return p;
    }
    const std::vector<FoldPlan>& barPlan() {
      static const std::vector<FoldPlan> p{
          {"Placement", "edge, size, length, margins", true, {"position", "size", "length", "align", "margin", "exclusive", "autohide", "layer", "screen"}},
          {"Look", "background, corners, border, text", true, {"bg", "radius", "border", "borderWidth", "line", "fg", "fontSize", "font"}},
          {"Spacing", "room at the ends and between entries", false, {"padding", "spacing"}},
      };
      return p;
    }
    // Fields grouped into plain-language folds, keeping their order. Empty folds are dropped.
    std::vector<std::pair<const FoldPlan*, std::vector<const Desc*>>> folds(const std::vector<Desc>& fields, const std::vector<FoldPlan>& plan) {
      std::vector<std::pair<const FoldPlan*, std::vector<const Desc*>>> out;
      for (const auto& f : plan) {
        std::vector<const Desc*> in;
        for (const auto& d : fields)
          if (std::ranges::find(f.keys, d.key) != f.keys.end()) in.push_back(&d);
        if (!in.empty()) out.emplace_back(&f, std::move(in));
      }
      return out;
    }

    // Editor state

    struct Ed {
      int bi = 0;
      std::string selSec;
      int selGi = -1;
      int selMi = -1;
      std::string addingTo; // Section whose "+" picker is open.

      [[nodiscard]] static json bars() {
        const json b = value("bars");
        return b.is_array() ? b : json::array();
      }
      [[nodiscard]] int bIndex() const {
        const int n = static_cast<int>(bars().size());
        return std::min(bi, std::max(0, n - 1));
      }
      [[nodiscard]] json bar() const {
        const json b = bars();
        const auto i = static_cast<std::size_t>(bIndex());
        return i < b.size() && b[i].is_object() ? b[i] : json::object();
      }
      [[nodiscard]] bool vertical() const {
        const json p = bar().value("position", json());
        return p == "left" || p == "right";
      }
      [[nodiscard]] std::string secName(const std::string& s) const {
        if (s == "center") return "Centre";
        if (s == "start") return vertical() ? "Top" : "Left";
        return vertical() ? "Bottom" : "Right";
      }
      void select(const std::string& sec, int gi, int mi) {
        selSec = sec;
        selGi = gi;
        selMi = mi;
        addingTo.clear();
      }
      void clearSel() {
        selSec.clear();
        selGi = -1;
        selMi = -1;
      }
      [[nodiscard]] std::string selKey() const {
        return selSec.empty() ? std::string() : selSec + ":" + std::to_string(selGi) + ":" + std::to_string(selMi);
      }

      // Runs fn on a copy of "bars" and writes it back. If fn returns false nothing is written.
      static void edit(const std::function<bool(json&)>& fn) {
        json d = bars();
        if (!fn(d)) return;
        set("bars", deepNormalized(d));
      }
      void editBar(const std::function<void(json&)>& fn) const {
        const int i = bIndex();
        edit([&](json& d) {
          if (static_cast<std::size_t>(i) >= d.size() || !d[static_cast<std::size_t>(i)].is_object()) return false;
          fn(d[static_cast<std::size_t>(i)]);
          return true;
        });
      }
      // The selected raw object inside d. Bare strings become objects so they can take options.
      json* target(json& d) const {
        const auto i = static_cast<std::size_t>(bIndex());
        if (i >= d.size() || !d[i].is_object() || selSec.empty()) return nullptr;
        json& b = d[i];
        if (!b.contains(selSec) || !b[selSec].is_array()) return nullptr;
        json& list = b[selSec];
        if (selGi < 0 || selGi >= static_cast<int>(list.size())) return nullptr;
        json& e = list[static_cast<std::size_t>(selGi)];
        e = norm(e);
        if (selMi < 0) return &e;
        if (!e.is_object() || !e.contains("modules") || !e["modules"].is_array() || selMi >= static_cast<int>(e["modules"].size())) return nullptr;
        json& m = e["modules"][static_cast<std::size_t>(selMi)];
        m = norm(m);
        return &m;
      }
      // The list holding the selection (a section, or its group's modules) and its index there.
      std::pair<json*, int> ownerList(json& d) const {
        const auto i = static_cast<std::size_t>(bIndex());
        if (i >= d.size() || !d[i].is_object() || selSec.empty()) return {nullptr, -1};
        json& b = d[i];
        if (!b.contains(selSec) || !b[selSec].is_array()) return {nullptr, -1};
        if (selMi < 0) return {&b[selSec], selGi};
        if (selGi < 0 || selGi >= static_cast<int>(b[selSec].size())) return {nullptr, -1};
        json& e = b[selSec][static_cast<std::size_t>(selGi)];
        e = norm(e);
        if (!e.is_object() || !e.contains("modules")) return {nullptr, -1};
        return {&e["modules"], selMi};
      }
      [[nodiscard]] json selected() const {
        json d = bars();
        json* t = target(d);
        return t != nullptr ? *t : json(nullptr);
      }
      [[nodiscard]] bool selIsGroup() const {
        const json s = selected();
        return s.is_object() && s.value("type", json()) == "group";
      }
      [[nodiscard]] std::string selKind() const {
        const json s = selected();
        if (s.is_null()) return {};
        return s.is_object() && s.value("type", json()) == "group" ? "group" : "module";
      }

      void setKey(const std::string& key, const json& v) const {
        edit([&](json& d) {
          json* t = target(d);
          if (t == nullptr) return true;
          if (removes(v)) {
            if (t->is_object()) t->erase(key);
          } else {
            (*t)[key] = v;
          }
          return true;
        });
      }
      void setBarKey(const std::string& key, const json& v) const {
        editBar([&](json& b) {
          if (removes(v)) b.erase(key);
          else b[key] = v;
        });
      }

      static json fresh(const std::string& type) {
        if (type == "group") return {{"type", "group"}, {"bg", "bg/0.6"}, {"radius", 8}, {"padding", {4, 4}}, {"modules", {"clock"}}};
        if (type == "custom") return {{"type", "custom"}, {"exec", "echo hello"}, {"interval", 10}};
        if (type == "text") return {{"type", "text"}, {"text", "text"}};
        return type;
      }
      void addEntry(const std::string& sec, const std::string& type) {
        const json l = bar().value(sec, json::array());
        const int n = l.is_array() ? static_cast<int>(l.size()) : 0;
        editBar([&](json& b) {
          if (!b.contains(sec) || !b[sec].is_array()) b[sec] = json::array();
          b[sec].push_back(fresh(type));
        });
        select(sec, n, -1);
      }
      void addToGroup(const std::string& type) {
        if (type == "group") return;
        const std::string sec = selSec;
        const int gi = selGi;
        const json b0 = bar();
        if (!b0.contains(sec) || !b0[sec].is_array() || gi < 0 || gi >= static_cast<int>(b0[sec].size())) return;
        const json e0 = norm(b0[sec][static_cast<std::size_t>(gi)]);
        const int n = e0.contains("modules") && e0["modules"].is_array() ? static_cast<int>(e0["modules"].size()) : 0;
        editBar([&](json& b) {
          json& e = b[sec][static_cast<std::size_t>(gi)];
          e = norm(e);
          if (!e.contains("modules") || !e["modules"].is_array()) e["modules"] = json::array();
          e["modules"].push_back(fresh(type));
        });
        select(sec, gi, n);
      }
      void move(int delta) {
        int ni = -1;
        edit([&](json& d) {
          auto [list, i] = ownerList(d);
          if (list == nullptr || !list->is_array()) return true;
          const int j = i + delta;
          if (i < 0 || i >= static_cast<int>(list->size()) || j < 0 || j >= static_cast<int>(list->size())) return true;
          const json x = (*list)[static_cast<std::size_t>(i)];
          list->erase(list->begin() + i);
          list->insert(list->begin() + j, x);
          ni = j;
          return true;
        });
        if (ni >= 0) {
          if (selMi < 0) selGi = ni;
          else selMi = ni;
        }
      }
      void toSection(const std::string& sec) {
        if (selMi >= 0 || sec == selSec) return;
        int n = 0;
        const std::string from = selSec;
        const int gi = selGi;
        editBar([&](json& b) {
          if (!b.contains(from) || !b[from].is_array() || gi < 0 || gi >= static_cast<int>(b[from].size())) return;
          const json x = b[from][static_cast<std::size_t>(gi)];
          b[from].erase(b[from].begin() + gi);
          if (!b.contains(sec) || !b[sec].is_array()) b[sec] = json::array();
          b[sec].push_back(x);
          n = static_cast<int>(b[sec].size()) - 1;
        });
        select(sec, n, -1);
      }
      // Drag and drop from the preview. Keys are "sec:gi:mi" (mi -1 for a section entry); `toK` may also
      // be "section:<name>".
      void moveByKeys(const std::string& fromK, const std::string& toK, bool after) {
        struct K {
          std::string sec;
          int gi = 0;
          int mi = 0;
        };
        auto pk = [](const std::string& k) {
          K out;
          const auto a = k.find(':'), b = k.find(':', a == std::string::npos ? a : a + 1);
          out.sec = k.substr(0, a);
          try {
            out.gi = std::stoi(k.substr(a + 1, b - a - 1));
            out.mi = std::stoi(k.substr(b + 1));
          } catch (...) {
          }
          return out;
        };
        const K f = pk(fromK);
        editBar([&](json& b) {
          if (!b.contains(f.sec) || !b[f.sec].is_array()) return;
          json& fl = b[f.sec];
          json item;
          int srcGroup = -1; // Index in b[f.sec] of the group the module came out of.
          if (f.mi < 0) {
            if (f.gi < 0 || f.gi >= static_cast<int>(fl.size())) return;
            item = fl[static_cast<std::size_t>(f.gi)];
            fl.erase(fl.begin() + f.gi);
          } else {
            if (f.gi < 0 || f.gi >= static_cast<int>(fl.size())) return;
            json& g = fl[static_cast<std::size_t>(f.gi)];
            g = norm(g);
            if (!g.contains("modules") || !g["modules"].is_array() || f.mi >= static_cast<int>(g["modules"].size())) return;
            item = g["modules"][static_cast<std::size_t>(f.mi)];
            g["modules"].erase(g["modules"].begin() + f.mi);
            srcGroup = f.gi;
          }
          const bool isGroup = norm(item).value("type", json()) == "group";
          auto insertAt = [](json& list, int at, const json& x) {
            at = std::clamp(at, 0, static_cast<int>(list.size()));
            list.insert(list.begin() + at, x);
            return at;
          };
          if (toK.starts_with("section:")) {
            const std::string s = toK.substr(8);
            if (!b.contains(s) || !b[s].is_array()) b[s] = json::array();
            b[s].push_back(item);
          } else {
            const K t = pk(toK);
            int tg = t.gi, tm = t.mi;
            // Account for the removal when the source was before the target in the same list.
            if (f.mi < 0 && f.sec == t.sec && f.gi < tg) tg--;
            if (f.mi >= 0 && t.mi >= 0 && f.sec == t.sec && f.gi == t.gi && f.mi < tm) tm--;
            if (!b.contains(t.sec) || !b[t.sec].is_array()) b[t.sec] = json::array();
            if (t.mi < 0 || isGroup) {
              const int at = insertAt(b[t.sec], std::max(0, tg) + (after ? 1 : 0), item);
              if (srcGroup >= 0 && t.sec == f.sec && at <= srcGroup) ++srcGroup;
            } else {
              json& tl = b[t.sec];
              const int idx = std::max(0, tg);
              if (idx >= static_cast<int>(tl.size())) return;
              json& g = tl[static_cast<std::size_t>(idx)];
              g = norm(g);
              if (!g.contains("modules") || !g["modules"].is_array()) return;
              insertAt(g["modules"], std::max(0, tm) + (after ? 1 : 0), item);
            }
          }
          // A group emptied by the move is removed.
          if (srcGroup >= 0 && srcGroup < static_cast<int>(b[f.sec].size())) {
            const json& g = b[f.sec][static_cast<std::size_t>(srcGroup)];
            if (g.is_object() && g.contains("modules") && g["modules"].is_array() && g["modules"].empty()) b[f.sec].erase(b[f.sec].begin() + srcGroup);
          }
        });
        clearSel();
      }
      void removeSel() {
        edit([&](json& d) {
          auto [list, i] = ownerList(d);
          if (list != nullptr && list->is_array() && i >= 0 && i < static_cast<int>(list->size())) list->erase(list->begin() + i);
          return true;
        });
        clearSel();
      }
      void duplicate() const {
        edit([&](json& d) {
          auto [list, i] = ownerList(d);
          if (list != nullptr && list->is_array() && i >= 0 && i < static_cast<int>(list->size()))
            list->insert(list->begin() + i + 1, json((*list)[static_cast<std::size_t>(i)]));
          return true;
        });
      }
      void wrap() const {
        if (selMi >= 0) return;
        const std::string sec = selSec;
        const int gi = selGi;
        editBar([&](json& b) {
          if (!b.contains(sec) || !b[sec].is_array() || gi < 0 || gi >= static_cast<int>(b[sec].size())) return;
          json& e = b[sec][static_cast<std::size_t>(gi)];
          e = json{{"type", "group"}, {"bg", "bg/0.6"}, {"radius", 8}, {"padding", {4, 4}}, {"modules", json::array({e})}};
        });
      }
      void unwrap() {
        if (selMi >= 0 || !selIsGroup()) return;
        const std::string sec = selSec;
        const int gi = selGi;
        editBar([&](json& b) {
          if (!b.contains(sec) || !b[sec].is_array() || gi < 0 || gi >= static_cast<int>(b[sec].size())) return;
          json& l = b[sec];
          const json g = norm(l[static_cast<std::size_t>(gi)]);
          const json mods = g.contains("modules") && g["modules"].is_array() ? g["modules"] : json::array();
          l.erase(l.begin() + gi);
          l.insert(l.begin() + gi, mods.begin(), mods.end());
        });
        clearSel();
      }
      void replaceSel(const json& obj) const {
        edit([&](json& d) {
          auto [list, i] = ownerList(d);
          if (list == nullptr || !list->is_array() || i < 0) return true;
          if (i >= static_cast<int>(list->size())) list->push_back(obj);
          else (*list)[static_cast<std::size_t>(i)] = obj;
          return true;
        });
      }
    };
    using EdPtr = std::shared_ptr<Ed>;

    // Layout pieces

    // Container rebuilt from scratch when its key changes, for structure that follows the layout (chips, folds).
    template <typename Base> class Rebuilt : public Base {
    public:
      template <typename... A>
      Rebuilt(std::function<std::string()> key, std::function<void(Rebuilt&)> build, A&&... args)
          : Base(std::forward<A>(args)...), m_key(std::move(key)), m_build(std::move(build)) {
        sync();
      }
      void sync() override {
        const std::string k = m_key();
        if (m_built && k == m_last) return;
        m_built = true;
        m_last = k;
        for (Item* it : this->items()) (void)this->removeChild(it);
        m_build(*this);
        sp::requestLayout();
      }

    private:
      std::function<std::string()> m_key;
      std::function<void(Rebuilt&)> m_build;
      std::string m_last;
      bool m_built = false;
    };

    // Children side by side, top-aligned unless asked to be centred.
    class TopRow : public Container {
    public:
      explicit TopRow(float spacing) : m_spacing(spacing) {}
      Item* centred(Item* it) {
        m_centred.push_back(it);
        return it;
      }
      float place(Renderer& renderer, float width) override {
        float x = 0.0F, h = 0.0F;
        bool first = true;
        std::vector<Item*> shown;
        for (Item* it : items()) {
          it->setVisible(it->shown());
          if (!it->shown()) continue;
          if (!first) x += m_spacing;
          first = false;
          h = std::max(h, it->place(renderer, width));
          it->setPosition(std::round(x), 0.0F);
          x += it->width();
          shown.push_back(it);
        }
        for (Item* it : shown)
          if (std::ranges::find(m_centred, it) != m_centred.end()) it->setPosition(it->x(), std::round((h - it->height()) / 2.0F));
        setSize(x, h);
        return h;
      }

    private:
      float m_spacing;
      std::vector<Item*> m_centred;
    };

    // Text vertically centred in a fixed height.
    class VText : public Text {
    public:
      VText(std::string text, float h, TextOpts opts) : Text(std::move(text), std::move(opts)), m_h(h) {}
      float place(Renderer& renderer, float width) override {
        Text::place(renderer, width);
        label()->setPosition(0.0F, std::round((m_h - label()->height()) / 2.0F));
        setSize(label()->width(), m_h);
        return m_h;
      }

    private:
      float m_h;
    };

    // Field row: the label (dim while unset) and the control, centred on a row at least 34 px high.
    class FieldRow : public Item {
    public:
      FieldRow(const Desc& d, std::function<json()> src, std::function<void(const std::string&, const json&)> setter)
          : m_desc(d), m_src(std::move(src)), m_setter(std::move(setter)) {
        m_label = static_cast<Label*>(addChild(makeText(d.label, 11.0F)));
        m_label->setMaxLines(0);
        auto cur = [this]() { return keyOf(m_src(), m_desc.key); };
        auto put = [this](const json& v) {
          if (m_setter) m_setter(m_desc.key, v);
        };
        const std::string& kind = d.kind;
        if (kind == "text") {
          const std::string key = d.key;
          m_field = static_cast<Field*>(addChild(std::make_unique<Field>(
              Binding{.get =
                          [cur]() -> json {
                            const json c = cur();
                            return unset(c) ? json("") : c.is_string() ? c : json(c.dump());
                          },
                      .set = put},
              FieldOpts{.width = 300.0F,
                        .placeholder = d.placeholder + "   ⏎ applies",
                        .convert = [key](const std::string& t) -> std::optional<json> {
                          if (t.empty()) return json(nullptr);
                          static const std::regex numeric("^-?\\d+(\\.\\d+)?$");
                          if (std::regex_match(t, numeric) && key != "format" && key != "text") {
                            try {
                              return json(std::stod(t));
                            } catch (...) {
                            }
                          }
                          return json(t);
                        }})));
          m_ctl = m_field;
        } else if (kind == "color") {
          m_ctl = static_cast<Item*>(addChild(std::make_unique<ColorPick>(Binding{
              .get = [cur]() -> json {
                const json c = cur();
                return unset(c) ? json(nullptr) : c;
              },
              .set = put})));
        } else if (kind == "int") {
          const json def = d.def;
          m_ctl = static_cast<Item*>(addChild(std::make_unique<Stepper>(
              Binding{.get =
                          [cur, def]() -> json {
                            const json c = cur();
                            if (!unset(c)) return c.is_number() ? c : json(std::atof(c.is_string() ? c.get<std::string>().c_str() : "0"));
                            return unset(def) ? json(0) : def;
                          },
                      .set = put},
              StepperOpts{.from = d.from, .to = d.to, .step = d.step, .suffix = "px"})));
        } else if (kind == "pair") {
          auto* row = static_cast<TopRow*>(addChild(std::make_unique<TopRow>(18.0F)));
          // An array as is, a number twice, else nothing.
          auto v = [cur]() -> json {
            const json c = cur();
            if (c.is_array()) return c;
            if (!unset(c)) return json::array({c, c});
            return nullptr;
          };
          auto pick = [](const json& a, std::size_t i) { return a.is_array() && a.size() > i ? a[i] : json(0); };
          row->add<Stepper>(Binding{.get = [v, pick]() -> json { return v().is_null() ? json(0) : pick(v(), 0); },
                                    .set = [v, pick, put](const json& n) { put(json::array({n, v().is_null() ? n : pick(v(), 1)})); }},
                            StepperOpts{.from = d.from, .to = d.to});
          row->add<Stepper>(Binding{.get = [v, pick]() -> json { return v().is_null() ? json(0) : pick(v(), 1); },
                                    .set = [v, pick, put](const json& n) { put(json::array({v().is_null() ? n : pick(v(), 0), n})); }},
                            StepperOpts{.from = d.from, .to = d.to});
          row->add<IconButton>(0xf0156, [put]() { put(json(nullptr)); })->showIf([cur]() { return !unset(cur()); });
          m_ctl = row;
        } else if (kind == "triple") {
          auto* row = static_cast<TopRow*>(addChild(std::make_unique<TopRow>(12.0F)));
          auto v = [cur]() {
            const json c = cur();
            auto n = [](const json& x) { return x.is_number() ? x : json(0); };
            if (c.is_number()) return json::array({c, c, c});
            if (c.is_array()) {
              if (c.size() >= 3) return json::array({n(c[0]), n(c[1]), n(c[2])});
              if (c.size() == 2) return json::array({n(c[0]), n(c[0]), n(c[1])});
              if (c.size() == 1) return json::array({n(c[0]), n(c[0]), n(c[0])});
            }
            if (c.is_object()) return json::array({c.value("edge", json(0)), c.value("inner", json(0)), c.value("sides", json(0))});
            return json::array({0, 0, 0});
          };
          for (std::size_t i = 0; i < 3; ++i) {
            row->add<Stepper>(Binding{.get = [v, i]() -> json { return v()[i]; },
                                      .set = [v, i, put](const json& n) {
                                        json t = v();
                                        t[i] = n;
                                        put(t);
                                      }},
                              StepperOpts{.from = 0, .to = d.to});
          }
          m_ctl = row;
        } else if (kind == "bool") {
          const json def = d.def;
          m_switch = static_cast<Switch*>(addChild(std::make_unique<Switch>(Binding{
              .get = [cur, def]() -> json {
                const json c = cur();
                return !unset(c) ? truthy(c) : (!unset(def) && truthy(def));
              },
              .set = put})));
          m_ctl = m_switch;
        } else if (kind == "enum") {
          const json def = d.def;
          m_seg = static_cast<Segmented*>(addChild(std::make_unique<Segmented>(
              Binding{.get =
                          [cur, def]() -> json {
                            const json c = cur();
                            return !unset(c) ? c : unset(def) ? json(nullptr) : def;
                          },
                      .set = put},
              d.options, 0.0F, 10.0F)));
          m_ctl = m_seg;
        } else if (kind == "caps") {
          m_seg = static_cast<Segmented*>(addChild(std::make_unique<Segmented>(
              Binding{.get =
                          [cur]() -> json {
                            const json c = cur();
                            return unset(c) ? json("none") : c;
                          },
                      .set = [put](const json& v) { put(v == "none" ? json(nullptr) : v); }},
              std::vector<Option>{{"none", "none"}, {"( ", "round"}, {"◀", "arrow"}, {"▶|", "arrow-in"}, {"◢", "slant"}, {"◥", "slant-back"}},
              0.0F, 11.0F)));
          m_ctl = m_seg;
        } else if (kind == "line") {
          auto* row = static_cast<TopRow*>(addChild(std::make_unique<TopRow>(10.0F)));
          auto l = [cur]() -> json {
            const json c = cur();
            return c.is_object() ? c : json(nullptr);
          };
          auto with = [l, put](const char* k, const json& v) {
            json o = l();
            if (!o.is_object()) o = json::object();
            o[k] = v;
            put(o);
          };
          row->centred(row->add<Switch>(Binding{
              .get = [l]() -> json { return !l().is_null(); },
              .set = [put](const json& v) { put(truthy(v) ? json{{"pos", "bottom"}, {"width", 2}, {"color", "accent"}} : json(nullptr)); }}));
          row->add<Segmented>(Binding{.get = [l]() -> json { return l().is_null() ? json("bottom") : l().value("pos", json("bottom")); },
                                      .set = [with](const json& v) { with("pos", v); }},
                              strOpts({"top", "bottom", "left", "right"}), 220.0F, 10.0F)
              ->showIf([l]() { return !l().is_null(); });
          row->add<Stepper>(Binding{.get = [l]() -> json { return l().is_null() ? json(2) : l().value("width", json(2)); },
                                    .set = [with](const json& n) { with("width", n); }},
                            StepperOpts{.from = 1, .to = 8})
              ->showIf([l]() { return !l().is_null(); });
          row->add<Field>(Binding{.get = [l]() -> json { return l().is_null() ? json("") : l().value("color", json("accent")); },
                                  .set = [with](const json& t) {
                                    const std::string s = t.is_string() ? t.get<std::string>() : std::string();
                                    with("color", s.empty() ? json("accent") : json(s));
                                  }},
                          FieldOpts{.width = 100.0F})
              ->showIf([l]() { return !l().is_null(); });
          m_ctl = row;
        }
        sync();
      }

      void sync() override {
        const bool has = !unset(keyOf(m_src(), m_desc.key));
        if (has != m_has || m_first) {
          m_first = false;
          m_has = has;
          m_label->setColor(has ? textA() : dim());
        }
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float ctlW = std::max(1.0F, width - 200.0F);
        // Controls fill the row's width; a switch's track stretches too.
        if (m_field != nullptr) m_field->setFixedWidth(ctlW);
        if (m_seg != nullptr) m_seg->setFixedWidth(ctlW);
        if (m_switch != nullptr) m_switch->setFixedWidth(ctlW);
        float ch = 0.0F;
        if (m_ctl != nullptr) ch = m_ctl->place(renderer, ctlW);
        const float h = std::max(34.0F, ch);
        m_label->setMaxWidth(190.0F);
        m_label->measure(renderer);
        m_label->setPosition(0.0F, std::round((h - m_label->height()) / 2.0F));
        if (m_ctl != nullptr) m_ctl->setPosition(200.0F, std::round((h - ch) / 2.0F));
        setSize(width, h);
        return h;
      }

    private:
      Desc m_desc;
      std::function<json()> m_src;
      std::function<void(const std::string&, const json&)> m_setter;
      Label* m_label = nullptr;
      Item* m_ctl = nullptr;
      Field* m_field = nullptr;
      Segmented* m_seg = nullptr;
      Switch* m_switch = nullptr;
      bool m_has = false;
      bool m_first = true;
    };

    // Raw JSON of the selection in a soft box that scrolls when it grows too tall. Ctrl+Enter applies.
    class JsonBox : public Item {
    public:
      JsonBox(EdPtr ed, std::function<void(const std::string&)> apply) : m_ed(std::move(ed)), m_apply(std::move(apply)) {
        m_bg = static_cast<Box*>(addChild(ui::box({})));
        m_bg->setRadius(10.0F);
        m_bg->setFill(textA(0.05F));
        m_bg->setBorder(textA(0.08F), 1.0F);
        m_measure = static_cast<Label*>(addChild(makeText("", 11.0F)));
        m_measure->setMaxLines(0);
        m_measure->setVisible(false);
        addChild(ui::input({
            .out = &m_input,
            .fontSize = 11.0F,
            .horizontalPadding = 0.0F,
            .clearButtonEnabled = false,
            .frameVisible = false,
            .onChange = [this](const std::string& t) {
              m_typed = t;
              if (m_measure->setText(t)) sp::requestLayout();
            },
            .onSubmit = [this](const std::string& t) {
              if (m_apply) m_apply(t);
            },
        }));
        m_input->setMultiline(true);
        m_input->setOnFocusGain([this]() {
          m_focused = true;
          m_bg->setBorder(accent(0.6F), 1.0F);
        });
        m_input->setOnFocusLoss([this]() {
          m_focused = false;
          m_bg->setBorder(textA(0.08F), 1.0F);
        });
        sync();
      }

      [[nodiscard]] std::string text() const { return m_input->value(); }

      void sync() override {
        // Replace the text only when the selection's JSON changes, so typing isn't overwritten.
        const json s = m_ed->selected();
        const std::string t = s.is_null() ? std::string() : s.dump(2);
        if (t == m_shown) return;
        m_shown = t;
        if (t != m_input->value()) {
          m_input->setValue(t);
          m_measure->setText(t);
          requestLayout();
        }
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        m_measure->setMaxWidth(std::max(1.0F, width - 20.0F));
        m_measure->measure(renderer);
        const float h = std::min(260.0F, std::max(90.0F, m_measure->height() + 20.0F));
        m_bg->setSize(width, h);
        // Input has its own inset; offset it so the text gets the box's 10 px margin.
        m_input->setPosition(10.0F - 5.0F, 10.0F - 10.0F);
        m_input->setSize(width - 20.0F + 5.0F, h - 20.0F + 10.0F);
        m_input->layout(renderer);
        setSize(width, h);
        return h;
      }

    private:
      EdPtr m_ed;
      std::function<void(const std::string&)> m_apply;
      Box* m_bg = nullptr;
      Label* m_measure = nullptr;
      Input* m_input = nullptr;
      std::string m_shown = "\x1f";
      std::string m_typed;
      bool m_focused = false;
    };

    // One section's row of chips with a "+" button, and the module type picker under it while open.
    class SectionRow : public Item {
    public:
      SectionRow(EdPtr ed, std::string sec) : m_ed(std::move(ed)), m_sec(std::move(sec)) {
        m_label = static_cast<Label*>(addChild(makeText("", 11.0F, true, dim())));
        m_label->setMaxLines(1);
        const std::string sec2 = m_sec;
        EdPtr e = m_ed;
        m_chips = static_cast<Rebuilt<Flow>*>(addChild(std::make_unique<Rebuilt<Flow>>(
            [e, sec2]() {
              // Rebuilt when the entries (types, group members) change.
              std::string k;
              const json l = e->bar().value(sec2, json::array());
              if (l.is_array())
                for (const auto& raw : l) {
                  const json en = norm(raw);
                  k += en.is_object() ? en.value("type", std::string("?")) : std::string("?");
                  if (en.is_object() && en.value("type", json()) == "group" && en.contains("modules") && en["modules"].is_array()) {
                    k += "[";
                    for (const auto& m : en["modules"]) {
                      const json mm = norm(m);
                      k += (mm.is_object() ? mm.value("type", std::string("?")) : std::string("?")) + ",";
                    }
                    k += "]";
                  }
                  k += ";";
                }
              return k;
            },
            [e, sec2](Rebuilt<Flow>& flow) {
              const json l = e->bar().value(sec2, json::array());
              const int n = l.is_array() ? static_cast<int>(l.size()) : 0;
              for (int gi = 0; gi < n; ++gi) {
                const json en = norm(l[static_cast<std::size_t>(gi)]);
                const std::string type = en.is_object() ? en.value("type", std::string()) : std::string();
                auto* row = flow.add<HRow>(3.0F);
                const auto [label, icon] = typeInfo(type);
                row->add<Chip>(type == "group" ? std::string("group") : label, icon)
                    ->onWhen([e, sec2, gi]() { return e->selSec == sec2 && e->selGi == gi && e->selMi < 0; })
                    ->onClick([e, sec2, gi]() {
                      e->select(sec2, gi, -1);
                      refresh();
                    });
                if (type == "group" && en.contains("modules") && en["modules"].is_array()) {
                  const int mn = static_cast<int>(en["modules"].size());
                  for (int mi = 0; mi < mn; ++mi) {
                    const json me = norm(en["modules"][static_cast<std::size_t>(mi)]);
                    const std::string mt = me.is_object() ? me.value("type", std::string()) : std::string();
                    row->add<Chip>("› " + typeInfo(mt).first)
                        ->onWhen([e, sec2, gi, mi]() { return e->selSec == sec2 && e->selGi == gi && e->selMi == mi; })
                        ->onClick([e, sec2, gi, mi]() {
                          e->select(sec2, gi, mi);
                          refresh();
                        });
                  }
                }
              }
              flow.add<Chip>("+")->onWhen([e, sec2]() { return e->addingTo == sec2; })->onClick([e, sec2]() {
                e->addingTo = e->addingTo == sec2 ? std::string() : sec2;
                refresh();
              });
            },
            5.0F)));
        m_picker = static_cast<Flow*>(addChild(std::make_unique<Flow>(5.0F)));
        for (const auto& t : types()) {
          const std::string type = t.t;
          m_picker->add<Chip>(t.label, t.icon)->onClick([e, sec2, type]() {
            e->addEntry(sec2, type);
            refresh();
          });
        }
        sync();
      }

      void sync() override {
        if (m_label->setText(m_ed->secName(m_sec) == "Centre" ? "Centre" : m_ed->secName(m_sec))) requestLayout();
        const bool open = m_ed->addingTo == m_sec;
        if (open != m_picker->visible()) {
          m_picker->setVisible(open);
          requestLayout();
        }
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float fh = m_chips->place(renderer, std::max(1.0F, width - 70.0F));
        m_label->measure(renderer);
        const float rowH = std::max(fh, m_label->height());
        m_label->setPosition(0.0F, std::round((rowH - m_label->height()) / 2.0F));
        m_chips->setPosition(70.0F, 0.0F);
        float h = rowH;
        if (m_picker->visible()) {
          const float ph = m_picker->place(renderer, std::max(1.0F, width - 70.0F));
          m_picker->setPosition(70.0F, h + 6.0F);
          h += 6.0F + ph;
        }
        setSize(width, h);
        return h;
      }

    private:
      EdPtr m_ed;
      std::string m_sec;
      Label* m_label = nullptr;
      Rebuilt<Flow>* m_chips = nullptr;
      Flow* m_picker = nullptr;
    };

  } // namespace

  void buildBarEditor(Column& col) {
    auto ed = std::make_shared<Ed>();
    EdPtr e = ed;

    {
      auto* g = col.add<Group>("Bars", "One spec per bar; each shows on every screen unless you name one.");
      g->add<Rebuilt<Flow>>(
          [e]() { return std::to_string(Ed::bars().size()); },
          [e](Rebuilt<Flow>& flow) {
            const int n = static_cast<int>(Ed::bars().size());
            for (int i = 0; i < n; ++i) {
              flow.add<Chip>("", 0xf04e9)
                  ->labelFrom([i]() {
                    const json b = Ed::bars();
                    const json p = static_cast<std::size_t>(i) < b.size() && b[static_cast<std::size_t>(i)].is_object()
                                       ? b[static_cast<std::size_t>(i)].value("position", json())
                                       : json();
                    return std::to_string(i + 1) + " · " + (p.is_string() && !p.get<std::string>().empty() ? p.get<std::string>() : "top");
                  })
                  ->onWhen([e, i]() { return i == e->bIndex(); })
                  ->onClick([e, i]() {
                    e->bi = i;
                    e->clearSel();
                    refresh();
                  });
            }
            flow.add<Chip>("Add bar", 0xf0415)->onClick([e]() {
              Ed::edit([](json& d) {
                bool top = false;
                for (const auto& b : d)
                  if (b.is_object() && b.value("position", json("top")) == "top") top = true;
                d.push_back({{"position", top ? "bottom" : "top"},
                             {"size", 28},
                             {"bg", "bg/0.6"},
                             {"module", {{"padding", {8, 8}}, {"gap", {0, 0}}}},
                             {"center", {"clock"}}});
                return true;
              });
              e->bi = static_cast<int>(Ed::bars().size());
              e->clearSel();
              refresh();
            });
            flow.add<Chip>("Remove this bar", 0xf0a7a)
                ->onClick([e]() {
                  const int i = e->bIndex();
                  Ed::edit([i](json& d) {
                    if (i < static_cast<int>(d.size())) d.erase(d.begin() + i);
                    return true;
                  });
                  e->bi = std::max(0, i - 1);
                  e->clearSel();
                  refresh();
                })
                ->showIf([]() { return Ed::bars().size() > 1; });
          },
          6.0F);
      g->add<BarPreview>([e]() { return e->bar(); },
                         [e](float) {
                           const json s = e->bar().value("size", json());
                           const float size = s.is_number() && s.get<float>() != 0.0F ? s.get<float>() : 28.0F;
                           return e->vertical() ? 260.0F : std::round((size + 16.0F) * 0.85F) + 6.0F;
                         },
                         // Close to real size; sections squeeze a little on narrow windows.
                         BarPreviewOpts{.pickable = true, .screenW = [](float w) { return std::round(w / 0.85F); }})
          ->selKey([e]() { return e->selKey(); })
          ->onPicked([e](const std::string& sec, int gi, int mi) {
            e->select(sec, gi, mi);
            refresh();
          })
          ->onMoved([e](const std::string& from, const std::string& to, bool after) {
            e->moveByKeys(from, to, after);
            refresh();
          });
      g->add<Text>("Click a module (or an island's edge) to edit it · drag it to move it, also into or out of islands.",
                   TextOpts{.px = 10.0F, .color = dim()});
    }

    {
      auto* g = col.add<Group>("Modules", "Click to select · + adds one · a group is an island holding several modules.");
      for (const char* sec : {"start", "center", "end"}) g->add<SectionRow>(e, sec);
    }

    // Inspector for the selection.
    {
      auto* g = col.add<Group>(" ", " ");
      g->showIf([e]() { return !e->selected().is_null(); });
      g->bindTitle([e]() {
        const json s = e->selected();
        if (s.is_null()) return std::string();
        const std::string t = s.is_object() ? s.value("type", std::string()) : std::string();
        return (t == "group" ? std::string("Group") : typeInfo(t).first) + (e->selMi >= 0 ? " (in a group)" : "");
      });
      g->bindHint([e]() {
        const json s = e->selected();
        if (s.is_null()) return std::string("Modules inside share this island; its style sits behind theirs.");
        const std::string t = s.is_object() ? s.value("type", std::string()) : std::string();
        return t != "group" ? typeDoc(t) : std::string("Modules inside share this island; its style sits behind theirs.");
      });

      auto* actions = g->add<Flow>(6.0F);
      actions->add<Chip>("", 0xf004d)
          ->labelFrom([e]() { return e->vertical() ? std::string("Up") : std::string("Left"); })
          ->onClick([e]() {
            e->move(-1);
            refresh();
          });
      actions->add<Chip>("", 0xf0054)
          ->labelFrom([e]() { return e->vertical() ? std::string("Down") : std::string("Right"); })
          ->onClick([e]() {
            e->move(1);
            refresh();
          });
      for (const char* sec : {"start", "center", "end"}) {
        const std::string s = sec;
        actions->add<Chip>("")
            ->labelFrom([e, s]() { return "→ " + e->secName(s); })
            ->onClick([e, s]() {
              e->toSection(s);
              refresh();
            })
            ->showIf([e, s]() { return e->selMi < 0 && e->selSec != s; });
      }
      actions->add<Chip>("Put in a group", 0xf0569)
          ->onClick([e]() {
            e->wrap();
            refresh();
          })
          ->showIf([e]() { return e->selMi < 0 && !e->selIsGroup(); });
      actions->add<Chip>("Ungroup", 0xf0e02)
          ->onClick([e]() {
            e->unwrap();
            refresh();
          })
          ->showIf([e]() { return e->selIsGroup(); });
      actions->add<Chip>("Duplicate", 0xf018f)->onClick([e]() {
        e->duplicate();
        refresh();
      });
      actions->add<Chip>("Remove", 0xf0a7a)->onClick([e]() {
        e->removeSel();
        refresh();
      });

      // For a group: add modules into it.
      auto* inside = g->add<Flow>(5.0F);
      inside->showIf([e]() { return e->selIsGroup(); });
      inside->add<VText>("Add inside:", 30.0F, TextOpts{.px = 11.0F, .color = dim()});
      for (const auto& t : types()) {
        if (std::string(t.t) == "group") continue;
        const std::string type = t.t;
        inside->add<Chip>(t.label)->onClick([e, type]() {
          e->addToGroup(type);
          refresh();
        });
      }

      // Type-specific bits the generic fields don't cover.
      auto src = [e]() { return e->selected(); };
      auto setter = [e](const std::string& k, const json& v) { e->setKey(k, v); };
      auto typeIs = [e](std::initializer_list<const char*> ts) {
        std::vector<std::string> v(ts.begin(), ts.end());
        return [e, v]() {
          const json s = e->selected();
          const std::string t = s.is_object() ? s.value("type", std::string()) : std::string();
          return !s.is_null() && std::ranges::find(v, t) != v.end();
        };
      };
      g->add<FieldRow>(Desc{.key = "text", .label = "Text", .kind = "text"}, src, setter)->showIf(typeIs({"text"}));
      g->add<FieldRow>(Desc{.key = "exec", .label = "Command", .kind = "text", .mono = true}, src, setter)->showIf(typeIs({"custom"}));
      g->add<FieldRow>(Desc{.key = "interval", .label = "Every (s, 0 = once)", .kind = "int", .def = 5, .from = 0, .to = 3600}, src, setter)
          ->showIf(typeIs({"custom"}));
      g->add<FieldRow>(Desc{.key = "stream", .label = "Keep running (line = update)", .kind = "bool"}, src, setter)->showIf(typeIs({"custom"}));
      g->add<FieldRow>(Desc{.key = "timeFormat", .label = "{time} format", .kind = "text", .placeholder = get<std::string>("bar.clock", "HH:mm"), .mono = true},
                       src, setter)
          ->showIf(typeIs({"clock"}));
      g->add<FieldRow>(Desc{.key = "dateFormat", .label = "{date} format", .kind = "text", .placeholder = "ddd d MMM", .mono = true}, src, setter)
          ->showIf(typeIs({"clock"}));
      g->add<FieldRow>(Desc{.key = "style", .label = "Style", .kind = "enum", .options = strOpts({"pills", "dots", "numbers", "roman", "kanji", "dwl"})},
                       src, setter)
          ->showIf(typeIs({"workspaces"}));
      g->add<FieldRow>(Desc{.key = "iconSize", .label = "Icon size", .kind = "int", .def = 16, .from = 10, .to = 48}, src, setter)
          ->showIf(typeIs({"tray", "taskbar"}));
      g->add<FieldRow>(Desc{.key = "titles", .label = "Show titles", .kind = "bool"}, src, setter)->showIf(typeIs({"taskbar"}));
      g->add<FieldRow>(Desc{.key = "size", .label = "Size", .kind = "int", .def = 8, .from = 0, .to = 400, .step = 4}, src, setter)
          ->showIf(typeIs({"spacer"}));

      // Rebuilt only when the kind of selection changes, so open folds stay open while editing.
      g->add<Rebuilt<Column>>(
          [e]() { return e->selKind(); },
          [e, src, setter](Rebuilt<Column>& c) {
            const std::string kind = e->selKind();
            if (kind.empty()) return;
            for (const auto& [plan, fields] : folds(kind == "group" ? groupFields() : moduleFields(), modulePlan())) {
              auto* fold = c.add<Fold>(plan->title, plan->hint, plan->open);
              for (const Desc* d : fields) {
                auto* row = fold->add<FieldRow>(*d, src, setter);
                if (d->key == "rotate") row->showIf([e]() { return e->vertical(); });
              }
            }
          },
          14.0F);

      // Anything else: the raw spec.
      g->add<Text>("JSON (states, when, colors, icons …)", TextOpts{.px = 11.0F, .color = dim(), .topPadding = 8.0F});
      auto error = std::make_shared<std::string>();
      auto apply = [e, error](const std::string& t) {
        const json obj = json::parse(t, nullptr, false);
        if (obj.is_discarded()) {
          *error = "JSON.parse: Parse error";
          refresh();
          return;
        }
        error->clear();
        e->replaceSel(obj);
        refresh();
      };
      auto* box = g->add<JsonBox>(e, apply);
      auto* row = g->add<HRow>(10.0F);
      row->add<Chip>("Apply JSON (Ctrl+Enter)", 0xf012c)->onClick([box, apply]() { apply(box->text()); });
      row->add<Text>("", TextOpts{.px = 11.0F, .color = danger()})->bindText([error]() { return *error; });
    }

    {
      auto* g = col.add<Group>("This bar");
      auto src = [e]() { return e->bar(); };
      auto setter = [e](const std::string& k, const json& v) { e->setBarKey(k, v); };
      for (const auto& [plan, fields] : folds(barFields(), barPlan())) {
        auto* fold = g->add<Fold>(plan->title, plan->hint, plan->open);
        for (const Desc* d : fields) {
          auto* row = fold->add<FieldRow>(*d, src, setter);
          if (d->key == "align")
            row->showIf([e]() {
              const json l = e->bar().value("length", json(json::value_t::discarded));
              return !unset(l) && l != json(0);
            });
        }
      }
    }

    {
      auto* flow = col.add<Flow>(6.0F);
      flow->add<Chip>("Edit settings.json", 0xf0dc9)->onClick([]() { spawn({"xdg-open", expandHome("~/.config/kusanagi/settings.json")}); });
      flow->add<Chip>("Format reference (docs/bar.md)", 0xf02d6)->onClick([]() { spawn({"xdg-open", expandHome("~/kusanagi/docs/bar.md")}); });
    }
  }

} // namespace kusanagi::sp
