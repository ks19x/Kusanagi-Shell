#include "launcher/kusanagi_provider.h"

#include "core/deferred_call.h"
#include "core/files/resource_paths.h"
#include "core/log.h"
#include "core/process/process.h"
#include "ipc/ipc_service.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "system/desktop_entry.h"
#include "util/string_utils.h"
#include "wayland/clipboard_service.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <sstream>

namespace {

  constexpr Logger kLog("kusanagi-launcher");
  constexpr std::size_t kMaxResults = 40;
  constexpr std::string_view kAppsProviderId = "Applications";

  std::string utf8(char32_t cp) {
    std::string out;
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
  }

  std::filesystem::path kusanagiConfigDir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
      return std::filesystem::path(xdg) / "kusanagi";
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home != nullptr ? home : "") / ".config" / "kusanagi";
  }

  std::string homeDir() {
    const char* home = std::getenv("HOME");
    return home != nullptr ? home : "";
  }

  std::string terminal() { return kusanagi::opt<std::string>("launcher", "terminal", "foot"); }

  std::string searchEngine() {
    return kusanagi::opt<std::string>("launcher", "searchEngine", "https://duckduckgo.com/?q=%s");
  }

  bool sortByUsage() { return kusanagi::opt<bool>("launcher", "sortByUsage", true); }

  // Match score, 0 for no match. An exact match beats a prefix, which beats a word start, then a
  // substring, then the query's letters appearing in order.
  double score(std::string_view text, std::string_view q) {
    if (text.empty() || q.empty()) {
      return 0.0;
    }
    const std::string t = StringUtils::toLower(text);
    if (t == q) {
      return 120.0;
    }
    const auto len = [](std::size_t n) { return static_cast<double>(n); };
    if (t.starts_with(q)) {
      return 100.0 - std::min(20.0, len(t.size() - q.size()));
    }
    // Word starts; words split on whitespace, '-', '_' and '.'.
    bool wordStart = true;
    for (std::size_t i = 0; i < t.size(); ++i) {
      const char c = t[i];
      const bool sep = std::isspace(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == '.';
      if (sep) {
        wordStart = true;
        continue;
      }
      if (wordStart && std::string_view(t).substr(i).starts_with(q)) {
        return 80.0;
      }
      wordStart = false;
    }
    if (const auto i = t.find(q); i != std::string::npos) {
      return 60.0 - std::min(20.0, len(i));
    }
    // Letters in order, e.g. "ffx" matches firefox.
    std::size_t k = 0;
    for (std::size_t j = 0; j < t.size() && k < q.size(); ++j) {
      if (t[j] == q[k]) {
        ++k;
      }
    }
    return k == q.size() ? 30.0 - std::min(20.0, t.size() >= q.size() ? len(t.size() - q.size()) : 0.0) : 0.0;
  }

  double usageBoost(int used) { return std::min(30.0, std::log2(static_cast<double>(used) + 1.0) * 8.0); }

  // A non-stable quicksort. Results with equal scores come out in the order the classic launcher always
  // showed them, which std::sort wouldn't reproduce.
  template <typename It, typename Less> void unstableSort(It start, It end, Less lessThan) {
    for (;;) {
      const auto span = end - start;
      if (span < 2) {
        return;
      }
      --end;
      It low = start;
      It high = end - 1;
      It pivot = start + span / 2;
      if (lessThan(*end, *start)) {
        std::iter_swap(end, start);
      }
      if (span == 2) {
        return;
      }
      if (lessThan(*pivot, *start)) {
        std::iter_swap(pivot, start);
      }
      if (lessThan(*end, *pivot)) {
        std::iter_swap(end, pivot);
      }
      if (span == 3) {
        return;
      }
      std::iter_swap(pivot, end);
      while (low < high) {
        while (low < high && lessThan(*low, *end)) {
          ++low;
        }
        while (high > low && lessThan(*end, *high)) {
          --high;
        }
        if (low < high) {
          std::iter_swap(low, high);
          ++low;
          --high;
        } else {
          break;
        }
      }
      if (lessThan(*low, *end)) {
        ++low;
      }
      std::iter_swap(end, low);
      unstableSort(start, low, lessThan);
      start = low + 1;
      ++end;
    }
  }

  // Calculator: numbers, + - * / % **, parentheses, pi, e and a few math functions.
  class Calc {
  public:
    explicit Calc(std::string_view s) : m_s(s) {}

    std::optional<double> run() {
      try {
        const double v = expr();
        skip();
        if (m_pos != m_s.size()) {
          return std::nullopt;
        }
        return v;
      } catch (...) {
        return std::nullopt;
      }
    }

  private:
    void skip() {
      while (m_pos < m_s.size() && (m_s[m_pos] == ' ' || m_s[m_pos] == '\t')) {
        ++m_pos;
      }
    }
    bool eat(std::string_view tok) {
      skip();
      if (m_s.substr(m_pos).starts_with(tok)) {
        m_pos += tok.size();
        return true;
      }
      return false;
    }
    double expr() {
      double v = term();
      for (;;) {
        if (eat("+")) {
          v += term();
        } else if (m_s.substr(m_pos).starts_with("-") && eat("-")) {
          v -= term();
        } else {
          return v;
        }
      }
    }
    double term() {
      double v = unary();
      for (;;) {
        skip();
        if (m_s.substr(m_pos).starts_with("**")) {
          return v;
        }
        if (eat("*")) {
          v *= unary();
        } else if (eat("/")) {
          v /= unary();
        } else if (eat("%")) {
          v = std::fmod(v, unary());
        } else {
          return v;
        }
      }
    }
    double unary() {
      if (eat("-")) {
        return -unary();
      }
      if (eat("+")) {
        return unary();
      }
      return power();
    }
    double power() {
      const double base = primary();
      if (eat("**")) {
        return std::pow(base, unary());
      }
      return base;
    }
    double primary() {
      skip();
      if (eat("(")) {
        const double v = expr();
        if (!eat(")")) {
          throw 0;
        }
        return v;
      }
      if (m_pos < m_s.size() && (std::isdigit(static_cast<unsigned char>(m_s[m_pos])) != 0 || m_s[m_pos] == '.')) {
        std::size_t end = m_pos;
        while (end < m_s.size() && (std::isdigit(static_cast<unsigned char>(m_s[end])) != 0 || m_s[end] == '.')) {
          ++end;
        }
        // Exponent, e.g. 1e5.
        if (end + 1 < m_s.size()
            && m_s[end] == 'e'
            && (std::isdigit(static_cast<unsigned char>(m_s[end + 1])) != 0 || m_s[end + 1] == '-' || m_s[end + 1] == '+')) {
          std::size_t e = end + 1;
          if (m_s[e] == '-' || m_s[e] == '+') {
            ++e;
          }
          while (e < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[e])) != 0) {
            ++e;
          }
          end = e;
        }
        const std::string num(m_s.substr(m_pos, end - m_pos));
        if (std::ranges::count(num, '.') > 1) {
          throw 0;
        }
        m_pos = end;
        return std::stod(num);
      }
      std::size_t end = m_pos;
      while (end < m_s.size() && std::isalpha(static_cast<unsigned char>(m_s[end])) != 0) {
        ++end;
      }
      if (end == m_pos) {
        throw 0;
      }
      const std::string word = StringUtils::toLower(m_s.substr(m_pos, end - m_pos));
      m_pos = end;
      if (word == "pi") {
        return M_PI;
      }
      if (word == "e") {
        return M_E;
      }
      std::vector<double> args;
      if (!eat("(")) {
        throw 0;
      }
      if (!eat(")")) {
        for (;;) {
          args.push_back(expr());
          if (eat(")")) {
            break;
          }
          if (!eat(",")) {
            throw 0;
          }
        }
      }
      const auto one = [&]() {
        if (args.empty()) {
          throw 0;
        }
        return args[0];
      };
      if (word == "sqrt") return std::sqrt(one());
      if (word == "sin") return std::sin(one());
      if (word == "cos") return std::cos(one());
      if (word == "tan") return std::tan(one());
      if (word == "log") return std::log(one());
      if (word == "abs") return std::fabs(one());
      if (word == "round") return std::floor(one() + 0.5);
      if (word == "floor") return std::floor(one());
      if (word == "ceil") return std::ceil(one());
      if (word == "pow") {
        if (args.size() < 2) throw 0;
        return std::pow(args[0], args[1]);
      }
      if (word == "min" || word == "max") {
        if (args.empty()) return word == "min" ? INFINITY : -INFINITY;
        return word == "min" ? *std::ranges::min_element(args) : *std::ranges::max_element(args);
      }
      throw 0;
    }

    std::string_view m_s;
    std::size_t m_pos = 0;
  };

  std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    for (std::size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size())) {
      s.replace(p, from.size(), to);
    }
    return s;
  }

  std::optional<std::string> calc(std::string_view input) {
    std::string e = StringUtils::trim(input);
    e = replaceAll(std::move(e), "^", "**");
    e = replaceAll(std::move(e), "×", "*");
    e = replaceAll(std::move(e), "÷", "/");
    if (e.empty()) {
      return std::nullopt;
    }
    for (const char c : e) {
      const bool ok = std::isalnum(static_cast<unsigned char>(c)) != 0 || std::string_view("+-*/%.() ,").contains(c);
      if (!ok) {
        return std::nullopt;
      }
    }
    const auto v = Calc(e).run();
    if (!v.has_value() || !std::isfinite(*v)) {
      return std::nullopt;
    }
    double r = std::round(*v * 1e10) / 1e10;
    if (r == 0.0) {
      r = 0.0; // no "-0"
    }
    if (std::fabs(r) < 1e21 && r == std::trunc(r)) {
      return std::format("{:.0f}", r);
    }
    return std::format("{}", r);
  }

  std::string urlEncode(std::string_view s) {
    std::string out;
    for (const unsigned char c : s) {
      if (std::isalnum(c) != 0 || std::string_view("-_.!~*'()").contains(static_cast<char>(c))) {
        out.push_back(static_cast<char>(c));
      } else {
        out += std::format("%{:02X}", c);
      }
    }
    return out;
  }

  // Uses fd (fdfind on Debian), else plain find. Prints paths relative to ~, folders with a trailing /.
  constexpr std::string_view kFindScript =
      "q=$1; cd \"$HOME\" || exit 1\n"
      "if command -v fd >/dev/null 2>&1; then f=fd; elif command -v fdfind >/dev/null 2>&1; then f=fdfind; else f=\"\"; fi\n"
      "if [ -n \"$f\" ]; then $f -i -F -H --max-results 40 -E .git -E node_modules -E .cache -E Trash -- \"$q\"\n"
      "else find . -maxdepth 6 \\( -name .cache -o -name .git -o -name node_modules -o -name Trash \\) -prune -o "
      "-iname \"*$q*\" -print 2>/dev/null | sed \"s|^[.]/||\" | head -40 | while IFS= read -r p; do if [ -d \"$p\" ]; "
      "then echo \"$p/\"; else echo \"$p\"; fi; done; fi";

  // Kusanagi's own commands
  struct Command {
    std::string name;
    std::string keys;
    char32_t icon;
    // One of ipc:<engine IPC line>, sh:<shell script>, msg:<kusanagi msg args>, run:<kusanagi args>,
    // preset:<id>, term:<script in a terminal> or fill:<query>.
    std::string action;
  };

  struct PresetInfo {
    const char* id;
    const char* name;
    const char* note;
  };

  // Built-in presets: id, name, note.
  constexpr PresetInfo kPresets[] = {
      {"kusanagi", "Kusanagi", "Translucent islands, pill workspaces, springy"},
      {"minimal", "Minimal", "dwl-style flat bar: tag blocks, window title, status text"},
      {"glass", "Floating glass", "A see-through floating bar, accent outlines, bouncy"},
      {"zen", "Zen", "Bottom bar, clock left, dots centred, almost no stats, unhurried"},
      {"terminal", "Terminal", "Square everything, mono palette, roman tags, instant"},
      {"neon", "Neon", "Tokyo Night, floating bar, glowing kanji tags, extra bouncy"},
      {"paper", "Paper", "Light Latte palette, calm solid bar, soft and readable"},
      {"hud", "Gamer HUD", "Every stat on show (GPU, temps), dwl tags, instant, no frills"},
      {"nordic", "Nordic", "Nord palette, clear text bar, clock left + numbers centred"},
      {"ink", "Ink", "Kanagawa colours, kanji tags, solid bottom bar, brushed calm"},
      {"aurora", "Aurora", "glass islands, gradient clock, Rosé Pine, smooth springs"},
      {"material", "Material You", "tonal accent chips, wallpaper colours, bouncy"},
      {"candy", "Candy", "colourful segments, Catppuccin Mocha, playful"},
      {"notch", "Notch", "a black island at the top, mono, side-sheet panel"},
      {"cyber", "Cyber", "neon slants, Tokyo Night, kanji, snappy"},
      {"win11", "Windows 11", "centred taskbar, start-menu launcher, side sheet"},
      {"zen2", "Zen pill", "one floating pill at the bottom, Gentle motion, Everforest"},
      {"powerline", "Powerline", "agnoster arrows: touching coloured segments, square and quick"},
      {"sidebar", "Sidebar", "a vertical bar on the left: pills, turned clock, stacked stats"},
      {"dock", "Dock", "slim top strip + a floating dock of open apps at the bottom"},
  };

  struct SettingsPage {
    const char* id;
    const char* name;
    char32_t icon;
    const char* keys;
  };

  // Settings pages: id, name, icon, search keywords.
  constexpr SettingsPage kSettingsPages[] = {
      {"presets", "Presets", 0xf0e09, "looks themes minimal dwl glass zen terminal save"},
      {"appearance", "Appearance", 0xf03d8, "theme colour accent font corners shadow animation speed"},
      {"bar", "Bar", 0xf04e9, "style islands solid floating modules clock scroll media marquee"},
      {"workspaces", "Workspaces", 0xf0570, "tags icons pills dots roman kanji glow"},
      {"panel", "Control panel", 0xf056e, "tiles width tab media stats"},
      {"wallpaper", "Wallpaper", 0xf0e09, "transition parallax slideshow fill dim awww picker"},
      {"launcher", "Launcher & clipboard", 0xf003b, "apps search clipboard terminal calculator"},
      {"lock", "Lock & power", 0xf033e,
       "hyprlock blur password test idle auto lock screen off dpms suspend sleep away polkit admin authentication"},
      {"login", "Login screen", 0xf0004, "greeter greetd display manager login session user boot"},
      {"notifications", "Notifications & OSD", 0xf009a, "popups dnd osd volume timeout position"},
      {"gamemode", "Game mode", 0xf0297, "games fullscreen performance governor feral blur"},
      {"recording", "Recording", 0xf044a, "record replay clip stream gpu screen recorder gsr wf-recorder video obs"},
      {"sound", "Sound", 0xf057e, "audio volume output input microphone apps"},
      {"bluetooth", "Bluetooth", 0xf00af,
       "bluetooth pair headphones earbuds headset mouse keyboard controller gamepad phone bluez"},
      {"display", "Display", 0xf0379,
       "monitor brightness dim ddc ddcutil backlight gaps borders windows night light gammastep resolution"},
      {"network", "Network", 0xf06f3, "ethernet wifi ip speed"},
      {"storage", "Storage", 0xf02ca, "disk xbps pacman apt dnf cache cleanup orphans kernels"},
      {"updates", "Updates", 0xf06b0, "packages upgrade xbps pacman apt dnf zypper flatpak aur"},
      {"about", "About", 0xf02fd, "system kusanagi version memory"},
  };

  std::string recorderMode() {
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    std::ifstream f(std::filesystem::path(rt != nullptr ? rt : "/tmp") / "kusanagi" / "record.mode");
    std::string mode;
    std::getline(f, mode);
    return StringUtils::trim(mode);
  }

  std::vector<Command> commands() {
    const std::string rec = recorderMode();
    std::vector<Command> c = {
        {"Lock screen", "lock away", 0xf033e, "ipc:session lock"},
        {"Power menu", "shut down power off reboot restart log out suspend sleep", 0xf0425, "ipc:panel-open session"},
        {"Suspend", "sleep", 0xf04b2, "sh:loginctl suspend || systemctl suspend"},
        {"Control panel", "quick settings tiles", 0xf056e, "ipc:panel-open control-center"},
        {"Notifications", "inbox", 0xf009a, "ipc:panel-open control-center notifications"},
        {"Do not disturb", "dnd silence notifications", 0xf009b, "ipc:notification-dnd-toggle"},
        {"Caffeine", "keep awake stay awake inhibit idle", 0xf0176, "ipc:caffeine-toggle"},
        {"Game mode", "gaming performance", 0xf0297, "msg:gamemode toggle"},
        {"Bluetooth", "bluetooth pair headphones earbuds connect", 0xf00af, "ipc:panel-open control-center bluetooth"},
        {"Brightness up", "brightness brighter screen monitor", 0xf00df, "ipc:brightness-up"},
        {"Brightness down", "brightness dimmer dim screen monitor", 0xf00dd, "ipc:brightness-down"},
        {"Wallpaper", "background picker", 0xf0e09, "ipc:panel-toggle wallpaper"},
        {"Clipboard history", "paste copy", 0xf0147, "ipc:panel-toggle clipboard"},
        {"Screenshot", "region capture print", 0xf0e51, "sh:sleep 0.3; kusanagi screenshot region"},
        {"Screenshot (whole screen)", "full capture print", 0xf0e51, "sh:sleep 0.3; kusanagi screenshot full"},
        {"Colour picker", "color pick eyedropper hex", 0xf020a, "sh:sleep 0.3; kusanagi colorpick"},
        {rec == "record" ? "Stop recording" : "Record the screen", "record video capture", 0xf044a, "run:record record"},
        {rec == "replay" ? "Stop the replay buffer" : "Start the replay buffer", "replay instant clip", 0xf0450,
         "run:record replay"},
        {"Save replay clip", "clip replay save", 0xf0fd8, "run:record save"},
        {"Check for updates", "packages upgrade", 0xf06b0, "msg:updates check"},
        {"Install updates", "packages upgrade system", 0xf06b0, "msg:updates upgrade"},
        {"Next preset", "look theme cycle", 0xf0e09, "run:preset next"},
        {"Kusanagi setup", "wizard welcome first", 0xf0493, "run:setup"},
        {"Kusanagi doctor", "check problems missing dependencies", 0xf04d9,
         "term:kusanagi doctor; echo; echo 'press Enter'; read x"},
        {"Emoji & symbols", "emoji symbol unicode character", 0xf0785, "fill::"},
        {"Search files", "find files documents", 0xf0214, "fill:/"},
    };
    for (const auto& p : kSettingsPages) {
      c.push_back({std::string("Settings: ") + p.name, std::string("settings ") + p.keys, p.icon,
                   std::string("run:settings ") + p.id});
    }
    for (const auto& p : kPresets) {
      c.push_back({std::string("Preset: ") + p.name, std::string("preset look theme ") + p.note, 0xf0e09,
                   std::string("preset:") + p.id});
    }
    // Saved presets from presets.json: {"saved": [{id, name, note}]}.
    try {
      std::ifstream f(kusanagiConfigDir() / "presets.json");
      if (f.is_open()) {
        const auto j = nlohmann::json::parse(f);
        const auto saved = j.value("saved", nlohmann::json::array());
        for (const auto& p : saved) {
          const std::string name = p.value("name", "");
          if (name.empty()) {
            continue;
          }
          c.push_back({"Preset: " + name, "preset look theme " + p.value("note", ""), 0xf0e09,
                       "preset:" + p.value("id", name)});
        }
      }
    } catch (...) {
    }
    return c;
  }

  // Runs `sh -c script` fully detached.
  void detachedShell(const std::string& script) {
    (void)process::runAsync(std::vector<std::string>{"sh", "-c", script});
  }

  std::string shellQuote(std::string_view s) {
    std::string out = "'";
    for (const char c : s) {
      if (c == '\'') {
        out += "'\\''";
      } else {
        out.push_back(c);
      }
    }
    out.push_back('\'');
    return out;
  }

} // namespace

KusanagiProvider::KusanagiProvider(ClipboardService* clipboard, IpcService* ipc) : m_clipboard(clipboard), m_ipc(ipc) {}

KusanagiProvider::~KusanagiProvider() = default;

void KusanagiProvider::reset() {
  // The symbol table and file results only live while the launcher is open.
  m_symbols.clear();
  m_symbols.shrink_to_fit();
  m_files.clear();
  m_fileQuery.clear();
  m_filePending.clear();
  m_fileDelay.stop();
  ++m_fileGeneration;
  m_usage.clear();
  m_usageLoaded = false;
}

KusanagiProvider::Mode KusanagiProvider::modeOf(std::string_view text) {
  if (text.empty()) {
    return Mode::Apps;
  }
  switch (text.front()) {
  case '=':
    return Mode::Calc;
  case '>':
    return Mode::Run;
  case ':':
    return Mode::Emoji;
  case '/':
    return Mode::Files;
  case '?':
    return Mode::Web;
  default:
    return Mode::Apps;
  }
}

std::string KusanagiProvider::fieldGlyph(Mode mode, bool actions) {
  if (actions) {
    return utf8(0xf0141);
  }
  switch (mode) {
  case Mode::Calc:
    return utf8(0xf00ec);
  case Mode::Run:
    return utf8(0xf018d);
  case Mode::Emoji:
    return utf8(0xf0785);
  case Mode::Files:
    return utf8(0xf0214);
  case Mode::Web:
    return utf8(0xf059f);
  case Mode::Apps:
    break;
  }
  return utf8(0xf0349);
}

bool KusanagiProvider::hasActions(const LauncherResult& result) {
  if (result.kind != "app") {
    return false;
  }
  for (const auto& e : desktopEntries()) {
    if (e.path == result.desktopEntryPath) {
      return !e.actions.empty();
    }
  }
  return false;
}

std::string KusanagiProvider::hintFor(const LauncherResult& result, bool hasActions) {
  if (result.kind == "calc") {
    return "copy  ↵";
  }
  if (result.kind == "emoji") {
    return "copy ↵  ·  type ⇧↵";
  }
  if (result.kind == "file") {
    return "open ↵  ·  folder ⇧↵";
  }
  return hasActions ? "→ actions  ↵" : "↵";
}

// Usage counts

void KusanagiProvider::loadUsage() const {
  if (m_usageLoaded) {
    return;
  }
  m_usageLoaded = true;
  m_usage.clear();
  try {
    std::ifstream f(kusanagiConfigDir() / "launcher-usage.json");
    if (!f.is_open()) {
      return;
    }
    const auto j = nlohmann::json::parse(f);
    const auto counts = j.value("counts", nlohmann::json::object());
    for (const auto& [key, value] : counts.items()) {
      if (value.is_number()) {
        m_usage[key] = value.get<int>();
      }
    }
  } catch (...) {
  }
}

void KusanagiProvider::saveUsage() const {
  nlohmann::json counts = nlohmann::json::object();
  for (const auto& [key, n] : m_usage) {
    counts[key] = n;
  }
  const auto dir = kusanagiConfigDir();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const auto tmp = dir / "launcher-usage.json.tmp";
  {
    std::ofstream f(tmp);
    if (!f.is_open()) {
      return;
    }
    f << nlohmann::json{{"counts", counts}}.dump(4) << '\n';
  }
  std::filesystem::rename(tmp, dir / "launcher-usage.json", ec);
}

int KusanagiProvider::usage(const std::string& key) const {
  loadUsage();
  const auto it = m_usage.find(key);
  return it != m_usage.end() ? it->second : 0;
}

void KusanagiProvider::recordUse(const LauncherResult& result) {
  std::string key;
  if (result.kind == "app" || result.kind == "action") {
    for (const auto& e : desktopEntries()) {
      if (e.path == result.desktopEntryPath) {
        key = e.id;
        break;
      }
    }
  } else if (result.kind == "emoji") {
    key = "sym:" + result.badge;
  }
  if (key.empty()) {
    return;
  }
  // Re-read first: the classic shell or another launcher session may have counted too.
  m_usageLoaded = false;
  loadUsage();
  ++m_usage[key];
  saveUsage();
}

// Search

std::vector<LauncherResult> KusanagiProvider::search(std::string_view text, std::string_view actionsOf) const {
  const Mode mode = modeOf(text);
  if (mode == Mode::Calc) {
    const std::string expr(text.substr(1));
    const auto r = calc(expr);
    if (!r.has_value()) {
      return {};
    }
    LauncherResult res;
    res.id = "calc:" + *r;
    res.kind = "calc";
    res.title = *r;
    res.subtitle = StringUtils::trim(expr) + "  =";
    res.iconGlyph = utf8(0xf00ec);
    return {std::move(res)};
  }
  if (mode == Mode::Run) {
    const std::string cmd = StringUtils::trim(text.substr(1));
    if (cmd.empty()) {
      return {};
    }
    LauncherResult res;
    res.id = "run:" + cmd;
    res.kind = "run";
    res.title = cmd;
    res.subtitle = "Enter to run  ·  Shift+Enter in a terminal";
    res.iconGlyph = utf8(0xf018d);
    return {std::move(res)};
  }
  if (mode == Mode::Emoji) {
    return symbolResults(StringUtils::toLower(StringUtils::trim(text.substr(1))));
  }
  if (mode == Mode::Files) {
    return fileResults(StringUtils::trim(text.substr(1)));
  }
  if (mode == Mode::Web) {
    const std::string w = StringUtils::trim(text.substr(1));
    if (w.empty()) {
      return {};
    }
    return {webItem(w)};
  }
  const std::string q = StringUtils::toLower(StringUtils::trim(text));
  if (!actionsOf.empty()) {
    return actionResults(actionsOf, q);
  }
  auto results = appResults(q);
  if (!q.empty() && kusanagi::opt<bool>("launcher", "webSearch", true)) {
    results.push_back(webItem(StringUtils::trim(text)));
  }
  return results;
}

std::vector<LauncherResult> KusanagiProvider::appResults(const std::string& q) const {
  const bool byUsage = sortByUsage();
  std::vector<LauncherResult> scored;
  std::set<std::string_view> seen;
  for (const DesktopEntry& e : desktopEntries()) {
    if (e.noDisplay || e.hidden || !seen.insert(e.id).second) {
      continue;
    }
    const int used = usage(e.id);
    double s = 0.0;
    if (q.empty()) {
      s = used > 0 ? 1000.0 + used : 0.0;
    } else {
      double kw = 0.0;
      std::string_view list = e.keywords;
      while (!list.empty()) {
        const auto semi = list.find(';');
        kw = std::max(kw, score(list.substr(0, semi), q));
        list = semi == std::string_view::npos ? std::string_view{} : list.substr(semi + 1);
      }
      s = std::max({score(e.name, q), score(e.genericName, q) * 0.8, kw * 0.7, score(e.id, q) * 0.6});
      if (s > 0.0 && byUsage) {
        s += usageBoost(used);
      }
    }
    if (s <= 0.0 && !q.empty()) {
      continue;
    }
    LauncherResult r;
    r.id = e.path;
    r.providerId = std::string(kAppsProviderId);
    r.kind = "app";
    r.desktopEntryPath = e.path;
    r.title = e.name;
    r.subtitle = !e.comment.empty() ? e.comment : e.genericName;
    r.iconName = e.icon.empty() ? "application-x-executable" : e.icon;
    r.iconGlyph = utf8(0xf003b);
    r.score = s;
    scored.push_back(std::move(r));
  }
  // Kusanagi's commands rank below equally good app matches, and only show once you type.
  if (!q.empty() && kusanagi::opt<bool>("launcher", "commands", true)) {
    for (auto& c : commands()) {
      const double s = std::max(score(c.name, q), score(c.keys, q) * 0.7) * 0.85;
      if (s < 25.0) {
        continue;
      }
      LauncherResult r;
      r.id = c.action;
      r.providerId = std::string(id());
      r.kind = "command";
      r.title = std::move(c.name);
      r.subtitle = "Kusanagi";
      r.iconGlyph = utf8(c.icon);
      r.score = s;
      scored.push_back(std::move(r));
    }
  }
  unstableSort(scored.begin(), scored.end(), [](const LauncherResult& a, const LauncherResult& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    return StringUtils::toLower(a.title) < StringUtils::toLower(b.title);
  });
  if (scored.size() > kMaxResults) {
    scored.resize(kMaxResults);
  }
  return scored;
}

std::vector<LauncherResult> KusanagiProvider::actionResults(std::string_view actionsOf, const std::string& q) const {
  std::vector<LauncherResult> out;
  for (const DesktopEntry& e : desktopEntries()) {
    if (e.path != actionsOf) {
      continue;
    }
    for (const DesktopAction& a : e.actions) {
      if (!q.empty() && score(a.name, q) <= 0.0) {
        continue;
      }
      LauncherResult r;
      r.id = "desktop-action:" + e.path + ":" + a.id;
      r.providerId = std::string(kAppsProviderId);
      r.kind = "action";
      r.desktopEntryPath = e.path;
      r.desktopActionId = a.id;
      r.title = a.name;
      r.subtitle = e.name;
      r.iconName = e.icon.empty() ? "application-x-executable" : e.icon;
      r.iconGlyph = utf8(0xf003b);
      out.push_back(std::move(r));
    }
    break;
  }
  return out;
}

void KusanagiProvider::loadSymbols() const {
  if (!m_symbols.empty()) {
    return;
  }
  std::ifstream f(paths::assetPath("symbols.tsv"));
  if (!f.is_open()) {
    kLog.warn("symbols.tsv not found");
    return;
  }
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    Symbol s;
    const auto t1 = line.find('\t');
    s.ch = line.substr(0, t1);
    if (t1 != std::string::npos) {
      const auto t2 = line.find('\t', t1 + 1);
      s.name = line.substr(t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1);
      if (t2 != std::string::npos) {
        s.keys = line.substr(t2 + 1);
      }
    }
    m_symbols.push_back(std::move(s));
  }
}

std::vector<LauncherResult> KusanagiProvider::symbolResults(const std::string& q) const {
  loadSymbols();
  struct Hit {
    const Symbol* sym;
    double s;
  };
  std::vector<Hit> hits;
  for (const Symbol& x : m_symbols) {
    const int used = usage("sym:" + x.ch);
    double sc = q.empty() ? (used > 0 ? 1000.0 + used : 0.0) : std::max(score(x.name, q), score(x.keys, q) * 0.6);
    if (!q.empty() && sc > 0.0) {
      sc += usageBoost(used);
    }
    if (sc > 0.0) {
      hits.push_back({&x, sc});
    }
  }
  unstableSort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.s > b.s; });
  const auto make = [](const Symbol& x, double s) {
    LauncherResult r;
    r.id = "sym:" + x.ch;
    r.kind = "emoji";
    r.title = x.name;
    r.subtitle = x.keys;
    r.badge = x.ch;
    r.score = s;
    return r;
  };
  std::vector<LauncherResult> out;
  // Nothing typed and nothing used yet: show the first symbols (the smileys).
  if (q.empty() && hits.empty()) {
    for (std::size_t i = 0; i < m_symbols.size() && i < 48; ++i) {
      out.push_back(make(m_symbols[i], 0.0));
    }
    return out;
  }
  for (std::size_t i = 0; i < hits.size() && i < 60; ++i) {
    out.push_back(make(*hits[i].sym, hits[i].s));
  }
  return out;
}

std::vector<LauncherResult> KusanagiProvider::fileResults(const std::string& q) const {
  if (q.size() < 2) {
    m_files.clear();
    m_fileQuery.clear();
    m_filePending.clear();
    m_fileDelay.stop();
    return {};
  }
  if (q != m_fileQuery && q != m_filePending) {
    startFileSearch(q);
  }
  return m_files;
}

void KusanagiProvider::startFileSearch(const std::string& q) const {
  m_filePending = q;
  const std::uint64_t gen = ++m_fileGeneration;
  // Search a moment after typing stops.
  m_fileDelay.start(std::chrono::milliseconds(220), [this, q, gen]() {
    process::RunCallbacks callbacks;
    auto out = std::make_shared<std::string>();
    callbacks.stdOut = [out](std::string_view chunk) { out->append(chunk); };
    callbacks.onExit = [this, q, gen, out](process::RunResult /*result*/) {
      DeferredCall::callLater([this, q, gen, text = std::move(*out)]() {
        if (gen != m_fileGeneration) {
          return; // a newer search started, or the launcher closed
        }
        const std::string home = homeDir();
        std::vector<LauncherResult> files;
        std::istringstream lines(text);
        std::string l;
        while (std::getline(lines, l)) {
          if (l.empty() || l == ".") {
            continue;
          }
          const bool dir = l.ends_with('/');
          const std::string path = dir ? l.substr(0, l.size() - 1) : l;
          const auto i = path.rfind('/');
          LauncherResult r;
          r.id = home + "/" + path;
          r.kind = "file";
          r.title = i != std::string::npos ? path.substr(i + 1) : path;
          r.subtitle = "~/" + (i != std::string::npos ? path.substr(0, i) : std::string());
          r.iconGlyph = utf8(dir ? 0xf024b : 0xf0214);
          files.push_back(std::move(r));
        }
        m_files = std::move(files);
        m_fileQuery = q;
        m_filePending.clear();
        if (m_resultsChanged) {
          m_resultsChanged();
        }
      });
    };
    process::RunOptions options;
    options.timeout = std::chrono::milliseconds(8000);
    options.maxOutputBytes = 256 * 1024;
    if (!process::runAsync(std::vector<std::string>{"sh", "-c", std::string(kFindScript), "sh", q}, std::move(callbacks),
                           std::move(options))) {
      kLog.warn("file search failed to start");
    }
  });
}

LauncherResult KusanagiProvider::webItem(const std::string& q) const {
  std::string domain = searchEngine();
  for (const std::string_view p : {"https://", "http://"}) {
    if (domain.starts_with(p)) {
      domain.erase(0, p.size());
      break;
    }
  }
  if (domain.starts_with("www.")) {
    domain.erase(0, 4);
  }
  domain = domain.substr(0, domain.find('/'));
  LauncherResult r;
  r.id = "web:" + q;
  r.kind = "web";
  r.title = "Search the web for “" + q + "”";
  r.subtitle = domain;
  r.iconGlyph = utf8(0xf059f);
  return r;
}

// Activation

bool KusanagiProvider::activate(const LauncherResult& result, bool alternate) {
  const std::string& kind = result.kind;
  if (kind == "calc") {
    return m_clipboard != nullptr && m_clipboard->copyText(result.title);
  }
  if (kind == "emoji") {
    if (alternate) {
      // Typing needs our window gone first, so focus is back where you were.
      (void)process::runAsync(std::vector<std::string>{
          "sh", "-c",
          "sleep 0.25; if command -v wtype >/dev/null; then wtype -- \"$1\"; else printf %s \"$1\" | wl-copy; fi", "sh",
          result.badge});
      return true;
    }
    return m_clipboard != nullptr && m_clipboard->copyText(result.badge);
  }
  if (kind == "file") {
    std::string target = result.id;
    if (alternate) {
      const auto slash = target.rfind('/');
      target = slash == std::string::npos || slash == 0 ? "/" : target.substr(0, slash);
    }
    return process::runAsync(std::vector<std::string>{"xdg-open", target});
  }
  if (kind == "web") {
    const std::string q = result.id.substr(4);
    std::string url = searchEngine();
    if (const auto p = url.find("%s"); p != std::string::npos) {
      url.replace(p, 2, urlEncode(q));
    } else {
      url += urlEncode(q);
    }
    return process::runAsync(std::vector<std::string>{"xdg-open", url});
  }
  if (kind == "run") {
    const std::string cmd = result.title;
    if (alternate) {
      return process::runAsync(std::vector<std::string>{terminal(), "-e", "sh", "-c", cmd + "; exec $SHELL"});
    }
    return process::runAsync(std::vector<std::string>{"sh", "-c", cmd});
  }
  if (kind == "command") {
    if (result.id.starts_with("fill:")) {
      if (m_queryRequested) {
        m_queryRequested(result.id.substr(5));
      }
      return false; // stay open
    }
    // The launcher closes first, then the command runs, so a panel it opens isn't closed with it.
    DeferredCall::callLater([this, id = result.id]() { runCommand(id); });
    return true;
  }
  return false;
}

void KusanagiProvider::runCommand(const std::string& id) {
  const auto colon = id.find(':');
  const std::string type = id.substr(0, colon);
  const std::string arg = colon == std::string::npos ? std::string() : id.substr(colon + 1);
  if (type == "ipc") {
    if (m_ipc != nullptr) {
      const std::string reply = m_ipc->execute(arg);
      if (reply.starts_with("error")) {
        kLog.warn("launcher command \"{}\": {}", arg, StringUtils::trim(reply));
      }
    }
  } else if (type == "sh") {
    detachedShell(arg);
  } else if (type == "msg") {
    detachedShell("kusanagi msg " + arg);
  } else if (type == "run") {
    detachedShell("kusanagi " + arg);
  } else if (type == "preset") {
    detachedShell("kusanagi preset " + shellQuote(arg));
  } else if (type == "term") {
    (void)process::runAsync(std::vector<std::string>{terminal(), "-e", "sh", "-c", arg});
  }
}
