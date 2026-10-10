#include "shell/bar/widgets/kusanagi_module_widget.h"

#include "shell/bar/widgets/kusanagi_taskbar.h"

#include "compositors/compositor_platform.h"
#include "core/deferred_call.h"
#include "core/files/resource_paths.h"
#include "core/process/process.h"
#include "dbus/bluetooth/bluetooth_service.h"
#include "dbus/mpris/mpris_service.h"
#include "dbus/upower/upower_service.h"
#include "idle/idle_inhibitor.h"
#include "notification/notification_manager.h"
#include "pipewire/pipewire_service.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "shell/kusanagi/game_mode.h"
#include "shell/kusanagi/kusanagi_ipc.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/media_popup.h"
#include "system/brightness_service.h"
#include "system/system_monitor_service.h"
#include "system/weather_service.h"
#include "ui/builders.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "wayland/wayland_connection.h"

#include "cursor-shape-v1-client-protocol.h"

#include <algorithm>
#include <arpa/inet.h>
#include <csignal>
#include <langinfo.h>
#include <linux/input-event-codes.h>
#include <locale.h>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <ifaddrs.h>
#include <net/if.h>
#include <regex>
#include <set>
#include <sstream>

using json = nlohmann::json;

namespace {

  std::string cp(char32_t c) {
    std::string out;
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
  }

  json icons(std::initializer_list<char32_t> list) {
    json a = json::array();
    for (char32_t c : list) {
      a.push_back(cp(c));
    }
    return a;
  }

  std::string str(const json& j, const char* key, const std::string& fallback = {}) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
      return fallback;
    }
    if (it->is_string()) {
      return it->get<std::string>();
    }
    if (it->is_number_integer()) {
      return std::to_string(it->get<long long>());
    }
    if (it->is_number()) {
      std::ostringstream o;
      o << it->get<double>();
      return o.str();
    }
    if (it->is_boolean()) return it->get<bool>() ? "true" : "false";
    return fallback;
  }

  // Stringifies a custom module's JSON field the way the classic shell did (arrays comma-joined).
  std::string jsString(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number()) {
      std::ostringstream o;
      o << v.get<double>();
      return o.str();
    }
    if (v.is_null()) return "null";
    if (v.is_array()) {
      std::string out;
      for (std::size_t i = 0; i < v.size(); ++i) out += (i > 0 ? "," : "") + (v[i].is_null() ? std::string() : jsString(v[i]));
      return out;
    }
    return "[object Object]";
  }

  // Length and prefix in characters, not bytes.
  std::size_t utf8Length(const std::string& s) {
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
  }
  std::string utf8Prefix(const std::string& s, std::size_t chars) {
    std::size_t i = 0;
    for (std::size_t n = 0; i < s.size() && n < chars; ++n) {
      ++i;
      while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    }
    return s.substr(0, i);
  }

  std::string trimmed(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
  }

  // The update check schedule while a bar shows the updates module: a first check a minute in, then every
  // updates.interval hours (0 = never). Shared by every updates module.
  struct UpdatesSchedule {
    int users = 0;
    int hours = -1;
    Timer first;
    Timer every;
  };
  UpdatesSchedule& updatesSchedule() {
    static auto* s = new UpdatesSchedule(); // never destroyed: its Timers mustn't outlive the TimerManager
    return *s;
  }
  void useUpdates(int delta) {
    auto& u = updatesSchedule();
    u.users += delta;
    const int hours = kusanagi::opt<int>("updates", "interval", 3);
    if (u.users <= 0 || hours <= 0) {
      u.every.stop();
      u.hours = -1;
      return;
    }
    if (kusanagi::updates::status().checkedAt == 0 && !u.first.active()) {
      u.first.start(std::chrono::seconds(60), []() {
        if (updatesSchedule().users > 0 && kusanagi::updates::status().checkedAt == 0) kusanagi::updates::check();
      });
    }
    if (hours != u.hours || !u.every.active()) {
      u.hours = hours;
      u.every.startRepeating(std::chrono::hours(hours), []() {
        if (updatesSchedule().users > 0) kusanagi::updates::check();
      });
    }
  }

  double num(const json& j, const char* key, double fallback) {
    auto it = j.find(key);
    if (it == j.end()) {
      return fallback;
    }
    if (it->is_number()) {
      return it->get<double>();
    }
    if (it->is_string()) {
      try {
        return std::stod(it->get<std::string>());
      } catch (...) {
      }
    }
    return fallback;
  }

  bool flag(const json& j, const char* key, bool fallback) {
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
  }

  // Formats a time with the clock's Qt-style patterns ("HH:mm", "ddd d MMM", "h:mm AP").
  std::string qtFormat(const std::string& fmt, const std::tm& t) {
    static const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char* months[] = {"January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December"};
    const bool twelve = fmt.find("AP") != std::string::npos || fmt.find("ap") != std::string::npos;
    auto pad = [](int v) { return (v < 10 ? "0" : "") + std::to_string(v); };
    std::string out;
    for (std::size_t i = 0; i < fmt.size();) {
      const char c = fmt[i];
      if (c == '\'') {
        const auto end = fmt.find('\'', i + 1);
        out += fmt.substr(i + 1, end == std::string::npos ? std::string::npos : end - i - 1);
        i = end == std::string::npos ? fmt.size() : end + 1;
        continue;
      }
      std::size_t n = 1;
      while (i + n < fmt.size() && fmt[i + n] == c) {
        ++n;
      }
      const int hour12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
      switch (c) {
      case 'H': out += n >= 2 ? pad(t.tm_hour) : std::to_string(t.tm_hour); break;
      case 'h': {
        const int h = twelve ? hour12 : t.tm_hour;
        out += n >= 2 ? pad(h) : std::to_string(h);
        break;
      }
      case 'm': out += n >= 2 ? pad(t.tm_min) : std::to_string(t.tm_min); break;
      case 's': out += n >= 2 ? pad(t.tm_sec) : std::to_string(t.tm_sec); break;
      case 'd':
        if (n >= 4) out += days[t.tm_wday];
        else if (n == 3) out += std::string(days[t.tm_wday]).substr(0, 3);
        else out += n == 2 ? pad(t.tm_mday) : std::to_string(t.tm_mday);
        break;
      case 'M':
        if (n >= 4) out += months[t.tm_mon];
        else if (n == 3) out += std::string(months[t.tm_mon]).substr(0, 3);
        else out += n == 2 ? pad(t.tm_mon + 1) : std::to_string(t.tm_mon + 1);
        break;
      case 'y': out += n >= 4 ? std::to_string(t.tm_year + 1900) : pad(t.tm_year % 100); break;
      case 'A':
        if (n == 1 && i + 1 < fmt.size() && fmt[i + 1] == 'P') {
          out += t.tm_hour < 12 ? "AM" : "PM";
          n = 2;
        } else {
          out.append(n, c);
        }
        break;
      case 'a':
        if (n == 1 && i + 1 < fmt.size() && fmt[i + 1] == 'p') {
          out += t.tm_hour < 12 ? "am" : "pm";
          n = 2;
        } else {
          out.append(n, c);
        }
        break;
      default: out.append(n, c);
      }
      i += n;
    }
    return out;
  }

  std::string escapeMarkup(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
      if (c == '&') out += "&amp;";
      else if (c == '<') out += "&lt;";
      else if (c == '>') out += "&gt;";
      else out += c;
    }
    return out;
  }

  std::string colorHex(const ColorSpec& spec) {
    const Color c = resolveColorSpec(spec);
    char buf[16];
    auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", b(c.r), b(c.g), b(c.b), b(c.a));
    return buf;
  }

  // Converts the rich text formats may use (<br>, <font color=...>) to Pango markup.
  std::string toPango(std::string s) {
    static const std::regex br("<br\\s*/?>");
    s = std::regex_replace(s, br, "\n");
    static const std::regex fontColor("<font\\s+color=([\"'])([^\"']+)\\1\\s*>");
    std::string out;
    std::sregex_iterator it(s.begin(), s.end(), fontColor), end;
    std::size_t last = 0;
    for (; it != end; ++it) {
      out += s.substr(last, it->position() - last);
      out += "<span foreground=\"" + colorHex(KusanagiModuleWidget::tokenColor((*it)[2].str(), colorSpecFromRole(ColorRole::OnSurface))) + "\">";
      last = it->position() + it->length();
    }
    out += s.substr(last);
    static const std::regex fontClose("</font>");
    return std::regex_replace(out, fontClose, "</span>");
  }

  // Fills in {var}, {var:N} (pad left) and {var:-N} (pad right).
  std::string render(const std::string& fmt, const json& vars) {
    static const std::regex token("\\{(\\w+)(?::(-?\\d+))?\\}");
    std::string out;
    std::sregex_iterator it(fmt.begin(), fmt.end(), token), end;
    std::size_t last = 0;
    for (; it != end; ++it) {
      out += fmt.substr(last, it->position() - last);
      last = it->position() + it->length();
      const std::string key = (*it)[1].str();
      if (!vars.contains(key)) {
        out += it->str();
        continue;
      }
      std::string v = str(vars, key.c_str());
      if ((*it)[2].matched) {
        const int w = std::stoi((*it)[2].str());
        const std::size_t len = v.size();
        if (w > 0 && len < static_cast<std::size_t>(w)) v = std::string(w - len, ' ') + v;
        if (w < 0 && len < static_cast<std::size_t>(-w)) v += std::string(-w - len, ' ');
      }
      out += escapeMarkup(v);
    }
    out += fmt.substr(last);
    return out;
  }

  std::string hm(std::int64_t seconds) {
    if (seconds <= 0) return {};
    // Always "Hh Mm".
    const auto h = seconds / 3600, m = (seconds % 3600) / 60;
    return std::to_string(h) + "h " + std::to_string(m) + "m";
  }

  // 1000-based, one decimal above bytes ("512B/s", "1.2MB/s").
  std::string rate(double bytesPerSec) {
    static const char* units[] = {"B", "kB", "MB", "GB"};
    int i = 0;
    while (bytesPerSec >= 1000.0 && i < 3) {
      bytesPerSec /= 1000.0;
      ++i;
    }
    char buf[32];
    if (i > 0) std::snprintf(buf, sizeof(buf), "%.1f%s/s", bytesPerSec, units[i]);
    else std::snprintf(buf, sizeof(buf), "%.0f%s/s", std::round(bytesPerSec), units[i]);
    return buf;
  }

  // The interface with the default route, whether it's Wi-Fi, and its IPv4 address.
  struct Net {
    std::string ifname;
    std::string ip;
    bool wifi = false;
  };
  Net defaultRoute() {
    Net n;
    std::ifstream route("/proc/net/route");
    std::string line;
    std::getline(route, line);
    while (std::getline(route, line)) {
      std::istringstream ss(line);
      std::string iface, dest;
      ss >> iface >> dest;
      if (dest == "00000000") {
        n.ifname = iface;
        break;
      }
    }
    if (n.ifname.empty()) return n;
    n.wifi = std::filesystem::exists("/sys/class/net/" + n.ifname + "/wireless");
    ifaddrs* addrs = nullptr;
    if (getifaddrs(&addrs) == 0) {
      for (auto* a = addrs; a != nullptr; a = a->ifa_next) {
        if (a->ifa_addr != nullptr && a->ifa_addr->sa_family == AF_INET && n.ifname == a->ifa_name) {
          char buf[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, buf, sizeof(buf));
          n.ip = buf;
          break;
        }
      }
      freeifaddrs(addrs);
    }
    return n;
  }

  std::string uptimeText() {
    std::ifstream f("/proc/uptime");
    double up = 0;
    f >> up;
    const long s = static_cast<long>(up), d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    // "[Dd ][Hh ]Mm"
    return (d > 0 ? std::to_string(d) + "d " : "") + (d > 0 || h > 0 ? std::to_string(h) + "h " : "") + std::to_string(m) + "m";
  }

  // /proc/loadavg's three averages, two spaces apart.
  std::string loadText() {
    std::ifstream f("/proc/loadavg");
    std::string a, b, c;
    f >> a >> b >> c;
    return a + "  " + b + "  " + c;
  }

  // The clock tooltip: this month as a <pre> grid (title centred on 20 columns, two-letter day names starting
  // from the locale's first weekday, today bold and underlined) with a small hint under it.
  std::string calendarMarkup(const std::tm& now) {
    static const locale_t loc = [] {
      locale_t l = newlocale(LC_TIME_MASK, "", static_cast<locale_t>(nullptr));
      return l != static_cast<locale_t>(nullptr) ? l : newlocale(LC_TIME_MASK, "C", static_cast<locale_t>(nullptr));
    }();
    int first = 0; // 0 = Sunday
#ifdef _NL_TIME_FIRST_WEEKDAY
    {
      const auto week1 = static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(nl_langinfo_l(_NL_TIME_WEEK_1STDAY, loc)));
      const int firstWeekday = *nl_langinfo_l(_NL_TIME_FIRST_WEEKDAY, loc);
      first = ((week1 == 19971201U ? 1 : 0) + firstWeekday - 1 + 7) % 7;
    }
#endif
    const int y = now.tm_year + 1900, mo = now.tm_mon, today = now.tm_mday;
    std::tm probe{};
    probe.tm_year = now.tm_year;
    probe.tm_mon = mo + 1;
    probe.tm_mday = 0;
    probe.tm_hour = 12;
    std::mktime(&probe);
    const int days = probe.tm_mday;
    probe = std::tm{};
    probe.tm_year = now.tm_year;
    probe.tm_mon = mo;
    probe.tm_mday = 1;
    probe.tm_hour = 12;
    std::mktime(&probe);
    const int lead = (probe.tm_wday - first + 7) % 7;
#ifdef ALTMON_1
    std::string month = nl_langinfo_l(static_cast<nl_item>(ALTMON_1 + mo), loc);
#else
    std::string month = nl_langinfo_l(static_cast<nl_item>(MON_1 + mo), loc);
#endif
    auto chars = [](const std::string& t) {
      return static_cast<int>(std::count_if(t.begin(), t.end(), [](char ch) { return (static_cast<unsigned char>(ch) & 0xC0) != 0x80; }));
    };
    const std::string title = escapeMarkup(month) + " " + std::to_string(y);
    std::string out = std::string(static_cast<std::size_t>(std::max(0, (20 - chars(month + " " + std::to_string(y))) / 2)), ' ') + title + "\n";
    for (int i = 0; i < 7; ++i) {
      std::string name = nl_langinfo_l(static_cast<nl_item>(ABDAY_1 + (first + i) % 7), loc);
      // The first two characters, not bytes.
      std::size_t at = 0;
      for (int n = 0; at < name.size() && n < 2; ++n) {
        ++at;
        while (at < name.size() && (static_cast<unsigned char>(name[at]) & 0xC0) == 0x80) ++at;
      }
      out += (i > 0 ? " " : "") + escapeMarkup(name.substr(0, at));
    }
    out += "\n";
    std::string line;
    for (int i = 0; i < lead; ++i) line += "   ";
    for (int d = 1; d <= days; ++d) {
      const std::string cell = (d < 10 ? " " : "") + std::to_string(d);
      line += d == today ? "<b><u>" + cell + "</u></b>" : cell;
      if ((lead + d) % 7 == 0) {
        out += line + "\n";
        line.clear();
      } else {
        line += " ";
      }
    }
    if (line.find_first_not_of(' ') != std::string::npos) {
      if (!line.empty() && line.back() == ' ') line.pop_back();
      out += line;
    }
    while (!out.empty() && out.back() == '\n') out.pop_back();
    // The tooltip manager turns <pre> and <small> into Pango markup (see tooltip_manager.cpp).
    return "<pre style=\"font-family:'" + kusanagi::font() + "'\">" + out + "</pre><small>click: control panel</small>";
  }

  // Provider tooltips are plain text: keep a stray '<' (in a window title, say) literal.
  std::string plainTooltip(const std::string& t) {
    if (t.find('<') == std::string::npos) return t;
    return "<span>" + escapeMarkup(t) + "</span>";
  }

} // namespace

ColorSpec KusanagiModuleWidget::tokenColor(const std::string& token, const ColorSpec& fallback) {
  return kusanagi_bar::color(token, fallback);
}

KusanagiModuleWidget::KusanagiModuleWidget(KusanagiModuleServices services, std::string specJson)
    : m_services(services) {
  try {
    m_spec = json::parse(specJson);
  } catch (...) {
    m_spec = json::object();
  }
  if (!m_spec.is_object()) m_spec = json::object();
  m_type = str(m_spec, "type", "text");
  if (m_spec.contains("padding") && m_spec["padding"].is_array() && m_spec["padding"].size() >= 2) {
    m_padStart = m_spec["padding"][0].get<float>();
    m_padEnd = m_spec["padding"][1].get<float>();
  }
  auto pairOf = [&](const char* key, float& a, float& b) {
    const auto& v = m_spec[key];
    if (v.is_array() && v.size() >= 2 && v[0].is_number() && v[1].is_number()) {
      a = v[0].get<float>();
      b = v[1].get<float>();
    } else if (v.is_array() && v.size() == 1 && v[0].is_number()) {
      a = b = v[0].get<float>();
    } else if (v.is_number()) {
      a = b = v.get<float>();
    }
  };
  pairOf("gap", m_gapStart, m_gapEnd);
  pairOf("inset", m_insetEdge, m_insetInner);
  pairOf("_groupInset", m_groupEdge, m_groupInner);
  m_farEdge = str(m_spec, "_edge") == "bottom" || str(m_spec, "_edge") == "right";
  if (m_services.sysmon != nullptr) {
    // Keep what the module and its tooltip show sampled: GPU load and VRAM, CPU and GPU temperature, disk usage.
    if (m_type == "gpu") m_services.sysmon->retainGpuUsage();
    if (m_type == "gpu") m_services.sysmon->retainGpuVram();
    if (m_type == "temp") m_services.sysmon->retainCpuTemp();
    if (m_type == "temp") m_services.sysmon->retainGpuTemp();
    if (m_type == "disk") m_services.sysmon->retainDiskPath(str(m_spec, "path", "/"));
    m_retained = m_type == "gpu" || m_type == "temp" || m_type == "disk";
  }
  if (m_type == "updates") useUpdates(+1);
  if (m_type == "taskbar") m_taskbar = std::make_unique<KusanagiTaskbar>(m_services.platform);
}

KusanagiModuleWidget::~KusanagiModuleWidget() {
  if (m_type == "custom") customStop(); // the command doesn't outlive its module
  if (m_type == "updates") useUpdates(-1);
  if (m_type == "media") KusanagiMediaPopup::instance().removeAnchor(m_area);
  if (m_retained && m_services.sysmon != nullptr) {
    if (m_type == "gpu") m_services.sysmon->releaseGpuUsage();
    if (m_type == "gpu") m_services.sysmon->releaseGpuVram();
    if (m_type == "temp") m_services.sysmon->releaseCpuTemp();
    if (m_type == "temp") m_services.sysmon->releaseGpuTemp();
    if (m_type == "disk") m_services.sysmon->releaseDiskPath(str(m_spec, "path", "/"));
  }
}

bool KusanagiModuleWidget::wantsSecondTick() const {
  // Clocks tick once a minute unless the format shows seconds, stats keep their pace, the rest is event-driven.
  if (m_type == "clock") {
    const std::string fmt = str(m_spec, "timeFormat", str(m_spec, "_clock", "HH:mm")) + str(m_spec, "format");
    if (fmt.find('s') != std::string::npos) return true;
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    return tm.tm_sec == 0;
  }
  static const std::set<std::string> timed{"cpu", "ram", "gpu", "temp", "disk", "network", "uptime", "battery",
                                           "weather", "media", "recorder", "updates", "title"};
  return timed.contains(m_type);
}

void KusanagiModuleWidget::create() {
  auto area = ui::inputArea({});
  m_area = area.get();
  // A pointing hand only when the module (or its provider) has a click action.
  area->setCursorShape(flag(m_spec, "_pointer", false) ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
  // "alt" toggles the alt format (formatAlt becomes when.alt). It's the module's own, not an engine action.
  std::uint32_t altButtons = 0;
  for (const auto& [key, button] : {std::pair{"click", BTN_LEFT}, {"rightClick", BTN_RIGHT}, {"middleClick", BTN_MIDDLE}}) {
    if (str(m_spec, key) == "alt") altButtons |= InputArea::buttonMask(button);
  }
  // The taskbar's items take every button the module itself has no action for (the bar strips the rest).
  const std::uint32_t taskbarButtons = m_type == "taskbar"
      ? InputArea::buttonMask(BTN_LEFT) | InputArea::buttonMask(BTN_RIGHT) | InputArea::buttonMask(BTN_MIDDLE) : 0;
  area->setAcceptedButtons(altButtons | taskbarButtons);
  if (altButtons != 0 || taskbarButtons != 0) {
    area->setOnClick([this, altButtons](const InputArea::PointerData& data) {
      if (m_taskbar != nullptr && m_taskbarNode != nullptr) {
        if (m_taskbar->click(data.localX - m_taskbarNode->x(), data.localY - m_taskbarNode->y(), data.button)) return;
        // Between the items, the press falls through to the group's actions.
        const auto gesture = kusanagi::bar::gestureForButton(data.button);
        const auto acts = m_spec.find("_groupActions");
        if (gesture && acts != m_spec.end() && acts->is_object()) {
          const auto it = acts->find(std::string(kusanagi::bar::gestureConfigKey(*gesture)));
          if (it != acts->end() && it->is_string()) {
            if (const auto action = kusanagi::bar::parseWidgetAction(it->get<std::string>()); action) (void)runGestureAction(*gesture, *action);
          }
        }
        return;
      }
      if ((altButtons & InputArea::buttonMask(data.button)) == 0) return;
      m_altOn = !m_altOn;
      m_lastMarkup.clear();
      requestUpdate();
    });
  }
  area->setOnEnter([this](const InputArea::PointerData&) {
    m_hovered = true;
    // Hovering a media module opens the media card unless "popup": false.
    if (m_type == "media") KusanagiMediaPopup::instance().onAnchorHover(m_area, true, flag(m_spec, "popup", true));
    requestUpdate();
  });
  area->setOnLeave([this]() {
    m_hovered = false;
    if (m_type == "media") KusanagiMediaPopup::instance().onAnchorHover(m_area, false, flag(m_spec, "popup", true));
    requestUpdate();
  });
  if (m_type == "media") KusanagiMediaPopup::instance().addAnchor(m_area);
  area->addChild(ui::label({
      .out = &m_label,
      .fontSize = Style::fontSizeBody,
      .fontWeight = labelFontWeight(),
      .fontFamily = labelFontFamily(),
      .maxLines = 1,
  }));
  m_label->setUseMarkup(true);
  // Multi-line formats ("{icon}\n{volume}") centre each line.
  m_label->setTextAlign(TextAlign::Center);
  if (m_type == "launcher" && flag(m_spec, "logo", false)) {
    area->addChild(ui::image({.out = &m_logo, .fit = ImageFit::Contain}));
  }
  if (m_taskbar != nullptr) m_taskbarNode = area->addChild(m_taskbar->create());
  // The module's own box sits behind the label.
  area->insertChildAt(0, m_box.create());
  setRoot(std::move(area));
}

KusanagiModuleWidget::Data KusanagiModuleWidget::collect(const json& o) const {
  Data d;
  const std::string& t = m_type;
  const auto stats = m_services.sysmon != nullptr ? std::optional<SystemStats>(m_services.sysmon->latest()) : std::nullopt;

  if (t == "clock") {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    d.vars["time"] = qtFormat(str(o, "timeFormat", str(o, "_clock", "HH:mm")), tm);
    d.vars["date"] = qtFormat(str(o, "dateFormat", "ddd d MMM"), tm);
    d.format = "{time}";
    d.tooltip = calendarMarkup(tm);
    d.tooltipRich = true;
  } else if (t == "volume" || t == "mic") {
    const AudioNode* node = nullptr;
    if (m_services.audio != nullptr) node = t == "mic" ? m_services.audio->defaultSource() : m_services.audio->defaultSink();
    d.shown = node != nullptr;
    const int v = node != nullptr ? static_cast<int>(std::lround(node->volume * 100.0F)) : 0;
    d.vars["volume"] = v;
    d.level = v;
    d.status = node != nullptr && node->muted ? "muted" : "";
    if (t == "volume") {
      d.icons = json::array({"", "", ""});
      d.format = d.status == "muted" ? "Muted" : "{icon}  {volume}%";
    } else {
      d.icons = json{{"muted", cp(0xf036d)}, {"default", cp(0xf036c)}};
      d.format = "{icon} {volume}%";
    }
  } else if (t == "cpu") {
    const int v = stats ? static_cast<int>(std::lround(stats->cpuUsagePercent)) : 0;
    d.vars["usage"] = v;
    d.level = v;
    d.format = "CPU {usage}%";
  } else if (t == "ram") {
    const int v = stats ? static_cast<int>(std::lround(stats->ramUsagePercent)) : 0;
    char used[16], total[16];
    std::snprintf(used, sizeof(used), "%.1f", stats ? static_cast<double>(stats->ramUsedMb) / 1024.0 : 0.0);
    std::snprintf(total, sizeof(total), "%.1f", stats ? static_cast<double>(stats->ramTotalMb) / 1024.0 : 0.0);
    d.vars["percent"] = v;
    d.vars["used"] = used;
    d.vars["total"] = total;
    char freeG[16];
    std::snprintf(freeG, sizeof(freeG), "%.1f", stats ? static_cast<double>(stats->ramTotalMb - stats->ramUsedMb) / 1024.0 : 0.0);
    d.vars["free"] = freeG;
    d.level = v;
    d.format = "RAM {percent}%";
    d.tooltip = std::string(used) + " / " + total + " GiB";
  } else if (t == "gpu") {
    const int v = stats && stats->gpuUsagePercent ? static_cast<int>(std::lround(*stats->gpuUsagePercent)) : 0;
    char used[16], total[16];
    std::snprintf(used, sizeof(used), "%.1f", stats && stats->gpuVramUsedBytes ? static_cast<double>(*stats->gpuVramUsedBytes) / 1073741824.0 : 0.0);
    std::snprintf(total, sizeof(total), "%.1f", stats && stats->gpuVramTotalBytes ? static_cast<double>(*stats->gpuVramTotalBytes) / 1073741824.0 : 0.0);
    d.vars["usage"] = v;
    d.vars["vramUsed"] = used;
    d.vars["vramTotal"] = total;
    d.level = v;
    d.format = "GPU {usage}%";
    if (stats && stats->gpuVramTotalBytes && *stats->gpuVramTotalBytes > 0) d.tooltip = "VRAM " + std::string(used) + " / " + total + " GiB";
  } else if (t == "temp") {
    const int cpu = stats && stats->cpuTempC ? static_cast<int>(std::lround(*stats->cpuTempC)) : 0;
    const int gpu = stats && stats->gpuTempC ? static_cast<int>(std::lround(*stats->gpuTempC)) : 0;
    const int v = str(o, "sensor") == "gpu" ? gpu : cpu;
    d.vars["temp"] = v;
    d.vars["cpu"] = cpu;
    d.vars["gpu"] = gpu;
    d.level = v;
    d.format = "{temp}°C";
    d.tooltip = "CPU " + std::to_string(cpu) + "°C   GPU " + std::to_string(gpu) + "°C";
  } else if (t == "disk") {
    const std::string path = str(m_spec, "path", "/");
    const auto ds = m_services.sysmon != nullptr ? m_services.sysmon->diskStats(path) : std::nullopt;
    // Like df: used and size in GiB, rounded; percent is used / size.
    const double usedG = ds ? static_cast<double>(ds->totalBytes - ds->freeBytes) / 1073741824.0 : 0.0;
    const double totalG = ds ? static_cast<double>(ds->totalBytes) / 1073741824.0 : 0.0;
    const double pct = totalG > 0 ? 100.0 * usedG / totalG : 0.0;
    d.vars["percent"] = static_cast<int>(std::lround(pct));
    d.vars["used"] = std::lround(usedG);
    d.vars["total"] = std::lround(totalG);
    d.vars["free"] = std::lround(totalG - usedG);
    d.tooltip = std::to_string(std::lround(usedG)) + " / " + std::to_string(std::lround(totalG)) + " GiB used";
    d.level = pct;
    d.format = cp(0xf02ca) + " {percent}%";
    d.shown = ds.has_value() && ds->totalBytes > 0;
  } else if (t == "network") {
    const Net n = defaultRoute();
    d.status = n.ifname.empty() ? "disconnected" : n.wifi ? "wifi" : "ethernet";
    d.icons = json{{"disconnected", cp(0xf05aa)}, {"wifi", ""}, {"ethernet", cp(0xf0200)}};
    d.vars["ifname"] = n.ifname;
    d.vars["ip"] = n.ip;
    d.vars["down"] = rate(stats ? stats->netRxBytesPerSec : 0.0);
    d.vars["up"] = rate(stats ? stats->netTxBytesPerSec : 0.0);
    d.format = "{icon}";
    if (!n.ifname.empty()) {
      d.tooltip = n.ifname + "  " + n.ip + "\n⇣ " + d.vars["down"].get<std::string>() + "  ⇡ " + d.vars["up"].get<std::string>();
    }
  } else if (t == "battery") {
    const auto* dev = m_services.upower != nullptr ? m_services.upower->defaultSystemBattery() : nullptr;
    const UPowerState st = m_services.upower != nullptr ? m_services.upower->state() : UPowerState{};
    d.shown = dev != nullptr && st.isPresent;
    const int pct = static_cast<int>(std::lround(st.percentage));
    const bool charging = st.state == BatteryState::Charging || st.state == BatteryState::PendingCharge;
    const bool full = st.state == BatteryState::FullyCharged;
    d.vars["capacity"] = pct;
    d.vars["time"] = hm(charging ? st.timeToFull : st.timeToEmpty);
    d.level = pct;
    d.lowIsBad = true;
    d.thresholds = json{{"warning", 30}, {"critical", 15}};
    d.status = full ? "full" : charging ? "charging" : "discharging";
    d.icons = icons({0xf008e, 0xf007a, 0xf007b, 0xf007c, 0xf007d, 0xf007e, 0xf007f, 0xf0080, 0xf0081, 0xf0082, 0xf0079});
    d.format = charging ? cp(0xf0084) + " {capacity}%" : "{icon} {capacity}%";
    if (const std::string time = d.vars["time"].get<std::string>(); !time.empty()) d.tooltip = time + (charging ? " until full" : " left");
  } else if (t == "media") {
    const auto player = m_services.mpris != nullptr ? m_services.mpris->activePlayer() : std::nullopt;
    std::string track;
    if (player) {
      std::string artists;
      for (const auto& a : player->artists) artists += (artists.empty() ? "" : ", ") + a;
      track = artists.empty() ? player->title : player->title + " - " + artists;
      if (!flag(o, "popup", true)) {
        std::size_t c60 = 0, j = 0;
        for (; j < track.size() && c60 < 60; ++j) {
          if ((static_cast<unsigned char>(track[j]) & 0xC0) != 0x80) ++c60;
        }
        while (j < track.size() && (static_cast<unsigned char>(track[j]) & 0xC0) == 0x80) ++j;
        d.tooltip = track.substr(0, j);
      }
      const std::size_t width = static_cast<std::size_t>(num(o, "width", kusanagi::opt<int>("bar", "mediaWidth", 20)));
      // Characters, not bytes: split on UTF-8 boundaries and keep the first 60.
      std::vector<std::string> cps;
      for (std::size_t i = 0; i < track.size() && cps.size() < 60;) {
        std::size_t n = 1;
        while (i + n < track.size() && (static_cast<unsigned char>(track[i + n]) & 0xC0) == 0x80) ++n;
        cps.push_back(track.substr(i, n));
        i += n;
      }
      std::string full;
      for (const auto& c : cps) full += c;
      // Marquee: while playing, a longer track scrolls as "title - artist" plus 5 spaces, rotated.
      const bool playing = player->playbackStatus == "Playing";
      const bool scrolls = flag(o, "marquee", kusanagi::opt<bool>("bar", "marquee", true)) && playing && cps.size() > width
          && !(GameModeService::instance() != nullptr && GameModeService::instance()->quiet());
      if (scrolls) {
        for (int k = 0; k < 5; ++k) cps.emplace_back(" ");
      }
      const std::size_t start = scrolls ? m_marquee % cps.size() : 0;
      track.clear();
      for (std::size_t k = 0; k < std::min(width, cps.size()); ++k) track += cps[(start + k) % cps.size()];
      d.vars["_marquee"] = scrolls;
      d.vars["_full"] = full;
      d.vars["title"] = player->title;
      d.vars["artist"] = artists;
      d.vars["album"] = player->album;
      d.vars["player"] = player->identity;
      d.status = player->playbackStatus == "Playing" ? "playing" : "paused";
    }
    d.vars["track"] = track;
    d.format = "♪  {track}";
    d.shown = !track.empty();
  } else if (t == "title") {
    // Overlays (launcher, panel) take focus without being windows, so keep the last real one while it lives.
    auto win = m_services.platform != nullptr ? m_services.platform->activeToplevel() : std::nullopt;
    if (win) {
      m_lastWindow = win;
    } else if (m_lastWindow && m_services.platform != nullptr) {
      bool alive = false;
      m_services.platform->wayland().visitWlrToplevels([&](const WlrToplevelSnapshot& w) {
        if (w.handle == m_lastWindow->handle) {
          alive = true;
          m_lastWindow->title = w.title;
          m_lastWindow->appId = w.appId;
        }
      });
      if (!alive) m_lastWindow.reset();
      win = m_lastWindow;
    }
    std::string full = win ? win->title : "";
    // In characters, not bytes. The default is the bar's title width setting.
    const auto max = static_cast<std::size_t>(std::max(1.0, num(o, "maxLength", kusanagi::opt<int>("bar", "titleWidth", 60))));
    if (utf8Length(full) > max) {
      d.tooltip = full; // the whole title when it's cut
      full = utf8Prefix(full, max - 1) + "…";
    }
    d.vars["title"] = full;
    d.vars["app"] = win ? win->appId : "";
    d.format = "{title}";
    d.shown = !full.empty();
  } else if (t == "notifications") {
    const auto count = m_services.notifications != nullptr ? m_services.notifications->history().size() : 0;
    const bool dnd = m_services.notifications != nullptr && m_services.notifications->doNotDisturb();
    d.vars["count"] = static_cast<int>(count);
    d.status = dnd ? "dnd" : count > 0 ? "unread" : "none";
    d.icons = json{{"dnd", cp(0xf009b)}, {"unread", cp(0xf116b)}, {"none", cp(0xf009a)}};
    d.format = "{icon}";
    d.tooltip = std::string(dnd ? "Do not disturb · " : "") + std::to_string(count) + " notification" + (count == 1 ? "" : "s");
  } else if (t == "caffeine") {
    const bool on = m_services.idle != nullptr && m_services.idle->enabled();
    d.status = on ? "on" : "off";
    d.icons = json{{"on", cp(0xf0176)}, {"off", cp(0xf0176)}};
    d.format = "{icon}";
    d.tooltip = on ? "Caffeine: the screen won't sleep or lock — click to turn off" : "Caffeine off — click to keep the screen awake";
    d.shown = on || flag(o, "always", false);
  } else if (t == "brightness") {
    double sum = 0;
    int n = 0;
    if (m_services.brightness != nullptr) {
      for (const auto& disp : m_services.brightness->displays()) {
        if (disp.controllable) {
          sum += disp.brightness;
          ++n;
          d.tooltip += (d.tooltip.empty() ? "" : "\n") + disp.label + ": " + std::to_string(std::lround(disp.brightness * 100.0F)) + "%";
        }
      }
    }
    const int pct = n > 0 ? static_cast<int>(std::lround(sum / n * 100.0)) : 0;
    d.vars["percent"] = pct;
    d.level = pct;
    d.icons = icons({0xf00dd, 0xf00de, 0xf00df});
    d.format = "{icon}  {percent}%";
    d.shown = n > 0;
  } else if (t == "bluetooth") {
    const BluetoothState* st = m_services.bluetooth != nullptr ? &m_services.bluetooth->state() : nullptr;
    const BluetoothDeviceInfo* first = nullptr;
    int connected = 0;
    if (m_services.bluetooth != nullptr) {
      for (const auto& dev : m_services.bluetooth->devices()) {
        if (dev.connected) {
          if (first == nullptr) first = &dev;
          ++connected;
        }
      }
    }
    const bool blocked = st != nullptr && (st->rfkillSoftBlocked || st->rfkillHardBlocked);
    d.shown = st != nullptr && st->adapterPresent;
    d.status = blocked ? "blocked" : st == nullptr || !st->powered ? "off" : first != nullptr ? "connected" : "on";
    d.vars["device"] = first != nullptr ? first->alias : "";
    d.vars["count"] = connected;
    d.vars["battery"] = first != nullptr && first->hasBattery ? std::to_string(first->batteryPercent) + "%" : "";
    d.icons = json{{"off", cp(0xf00b2)}, {"blocked", cp(0xf00b2)}, {"on", cp(0xf00af)}, {"connected", cp(0xf00b1)}};
    d.format = d.status == "connected" ? "{icon} {device}" : "{icon}";
    // Bt.summary, the first device's battery, every connected device when there are several
    {
      const bool on = st != nullptr && st->powered;
      std::string summary = st == nullptr || !st->adapterPresent ? "No adapter" : blocked ? "Blocked" : !on ? "Off"
          : connected == 1 ? first->alias : connected > 1 ? std::to_string(connected) + " devices" : "On";
      d.tooltip = "Bluetooth: " + summary + (first != nullptr && first->hasBattery ? " · " + std::to_string(first->batteryPercent) + "%" : "");
      if (connected > 1) {
        std::string names;
        for (const auto& dev : m_services.bluetooth->devices()) {
          if (dev.connected) names += (names.empty() ? "" : ", ") + dev.alias;
        }
        d.tooltip += "\n" + names;
      }
    }
  } else if (t == "weather") {
    const bool has = m_services.weather != nullptr && m_services.weather->hasData();
    d.shown = has;
    if (has) {
      const auto& snap = m_services.weather->snapshot();
      const auto& cur = snap.current;
      d.vars["temp"] = static_cast<int>(std::lround(m_services.weather->displayTemperature(cur.temperatureC)));
      d.vars["unit"] = m_services.weather->displayTemperatureUnit();
      d.vars["feels"] = static_cast<int>(std::lround(m_services.weather->displayTemperature(cur.apparentTemperatureC.value_or(cur.temperatureC))));
      d.vars["desc"] = WeatherService::shortDescriptionForCode(cur.weatherCode);
      d.vars["place"] = snap.locationName;
      d.tooltip = snap.locationName + ": " + d.vars["desc"].get<std::string>() + ", feels " + std::to_string(d.vars["feels"].get<int>())
          + d.vars["unit"].get<std::string>();
    }
    d.icon = cp(0xf0590);
    d.format = "{icon} {temp}{unit}";
  } else if (t == "uptime") {
    d.vars["uptime"] = uptimeText();
    d.vars["load"] = loadText();
    d.format = cp(0xf0954) + " {uptime}";
    d.tooltip = "load " + d.vars["load"].get<std::string>();
  } else if (t == "sep") {
    d.format = "│";
    d.fg = "faint";
  } else if (t == "spacer") {
    d.format = "";
    d.shown = true;
    d.vars["_size"] = num(o, "size", 8);
  } else if (t == "text") {
    d.format = str(o, "text");
  } else if (t == "launcher") {
    d.format = cp(0xf003b);
    d.tooltip = "Apps (right-click: Settings)";
  } else if (t == "power") {
    d.format = "⏻";
  } else if (t == "gamemode") {
    const auto* gm = GameModeService::instance();
    const bool on = gm != nullptr && gm->active();
    d.status = on ? "on" : "off";
    d.format = cp(0xf0297);
    d.shown = on || flag(o, "always", false);
    d.tooltip = on ? "Game mode on" : "Game mode off";
  } else if (t == "recorder") {
    // `kusanagi record` (scripts/record) rewrites this after every action
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    std::ifstream f(std::string(rt != nullptr ? rt : "/tmp") + "/kusanagi/record.json");
    json st = f ? json::parse(f, nullptr, false) : json();
    const std::string mode = st.is_object() ? st.value("mode", "off") : "off";
    std::string elapsed;
    if (mode != "off" && st.is_object() && st.value("since", 0.0) > 0) {
      const long secs = std::max(0L, static_cast<long>(std::time(nullptr) - static_cast<long>(st.value("since", 0.0))));
      char buf[32];
      if (secs >= 3600) std::snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", secs / 3600, (secs % 3600) / 60, secs % 60);
      else std::snprintf(buf, sizeof(buf), "%ld:%02ld", secs / 60, secs % 60);
      elapsed = buf;
    }
    d.status = mode;
    d.icons = json{{"off", cp(0xf044a)}, {"replay", cp(0xf0450)}, {"record", cp(0xf044a)}, {"stream", cp(0xf0567)}};
    d.vars["time"] = mode == "replay" ? "" : elapsed;
    d.vars["mode"] = mode;
    d.vars["backend"] = st.is_object() ? st.value("backend", "") : "";
    d.format = "{icon} {time}";
    d.shown = mode != "off" || flag(o, "always", false);
    d.tooltip = mode == "replay" ? "Replay buffer on — click to save the last " + std::to_string(kusanagi::opt<int>("recorder", "replay", 30)) + " s"
        : mode == "record"       ? "Recording " + elapsed + " — click to stop"
        : mode == "stream"       ? "Streaming " + elapsed + " — click to stop"
                                 : std::string("Not recording — click to start");
  } else if (t == "updates") {
    // The count from this shell's last update check (kusanagi_ipc.h), "?" until one succeeded.
    const auto st = kusanagi::updates::status();
    d.vars["count"] = st.known ? std::to_string(st.count) : "?";
    d.level = std::min(100, st.count);
    d.status = !st.known ? "unknown" : st.count == 0 ? "none" : st.count >= 50 ? "many" : "some";
    d.format = cp(0xf06b0) + " {count}";
    d.shown = st.count > 0 || flag(o, "always", false);
    if (st.checking) {
      d.tooltip = "Checking for updates…";
    } else if (!st.known) {
      d.tooltip = "Couldn't check for updates (see: kusanagi updates)";
    } else if (st.count == 0) {
      d.tooltip = "Up to date";
    } else {
      d.tooltip = std::to_string(st.count) + (st.count == 1 ? " update" : " updates") + " — click to install\n";
      for (std::size_t k = 0; k < st.list.size() && k < 12; ++k) {
        const auto& [name, source] = st.list[k];
        d.tooltip += (k > 0 ? "\n" : "") + name + (source.empty() ? "" : "  ·  " + source);
      }
      if (st.count > 12) d.tooltip += "\n…";
    }
  } else if (t == "taskbar") {
    d.format = "";
    d.shown = true; // while it has something to show (syncTaskbar)
  } else if (t == "custom") {
    // The command's last line: plain text, or JSON {text, tooltip, class, percentage, alt, ...} with every
    // field available as a var.
    d.vars = m_customData;
    if (m_customData.contains("percentage")) d.level = num(m_customData, "percentage", -1.0);
    if (const auto c = m_customData.find("class"); c != m_customData.end()) {
      if (c->is_array()) {
        for (const auto& x : *c) d.status += (d.status.empty() ? "" : " ") + jsString(x);
      } else if (!c->is_null()) {
        d.status = jsString(*c);
      }
    }
    d.format = "{text}";
    if (const auto tip = m_customData.find("tooltip"); tip != m_customData.end()) d.tooltip = jsString(*tip);
    d.shown = !str(o, "exec").empty();
  } else {
    d.format = str(o, "text");
  }
  return d;
}

json KusanagiModuleWidget::effective(const Data& data, std::vector<std::string>& states) const {
  // Threshold state: the highest (or for lowIsBad, lowest) threshold the level passed.
  const json th = m_spec.contains("states") && m_spec["states"].is_object() ? m_spec["states"] : data.thresholds;
  if (th.is_object() && data.level >= 0) {
    std::string best;
    double bestV = data.lowIsBad ? INFINITY : -INFINITY;
    for (auto it = th.begin(); it != th.end(); ++it) {
      if (!it->is_number()) continue;
      const double v = it->get<double>();
      if (data.lowIsBad ? (data.level <= v && v < bestV) : (data.level >= v && v > bestV)) {
        best = it.key();
        bestV = v;
      }
    }
    if (!best.empty()) states.push_back(best);
  }
  std::istringstream ss(data.status);
  for (std::string s; ss >> s;) states.push_back(s);
  if (m_altOn) states.push_back("alt");
  if (m_hovered) states.push_back("hover");

  json eff = m_spec;
  if (m_spec.contains("when") && m_spec["when"].is_object()) {
    for (const auto& s : states) {
      if (auto it = m_spec["when"].find(s); it != m_spec["when"].end() && it->is_object()) {
        for (auto kv = it->begin(); kv != it->end(); ++kv) eff[kv.key()] = kv.value();
      }
    }
  }
  return eff;
}

void KusanagiModuleWidget::doUpdate(Renderer& renderer) {
  if (m_label == nullptr) return;
  Data data = collect(m_spec);
  if (m_type == "media") {
    // The marquee restarts with each track and ticks only while it scrolls.
    const std::string full = data.vars.value("_full", std::string());
    if (full != m_marqueeTrack) {
      m_marqueeTrack = full;
      if (m_marquee != 0) {
        m_marquee = 0;
        data = collect(m_spec);
      }
    }
    if (data.vars.value("_marquee", false)) {
      if (!m_marqueeTimer.active()) {
        m_marqueeTimer.startRepeating(std::chrono::milliseconds(150), [this]() {
          ++m_marquee;
          requestUpdate();
        });
      }
    } else {
      m_marqueeTimer.stop();
    }
  }
  std::vector<std::string> states;
  json eff = effective(data, states);
  // The provider reads its options from the effective spec, since a state may change them.
  if (eff != m_spec) {
    const Data again = collect(eff);
    if (again.vars != data.vars || again.format != data.format || again.tooltip != data.tooltip || again.shown != data.shown
        || again.status != data.status || again.level != data.level) {
      data = again;
      states.clear();
      eff = effective(data, states);
    }
  }
  m_states = states;
  if (m_type == "custom") customSync(eff);
  if (m_area != nullptr) {
    // A pointing hand when the module (in this state) or its provider has a click action.
    const bool pointer = eff.contains("click") || flag(m_spec, "_pointer", false);
    m_area->setCursorShape(pointer ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
  }

  // {icon}: an icons array indexed by level, or a map by state (latest state wins), else the provider's icon.
  std::string icon = data.icon;
  const json ic = eff.contains("icons") && !eff["icons"].is_null() ? eff["icons"] : data.icons;
  if (ic.is_array() && !ic.empty()) {
    const auto n = static_cast<int>(ic.size());
    const int i = std::clamp(static_cast<int>(std::floor(std::max(0.0, data.level) / 100.0 * n)), 0, n - 1);
    icon = ic[i].is_string() ? ic[i].get<std::string>() : "";
  } else if (ic.is_object()) {
    bool found = false;
    for (auto it = states.rbegin(); it != states.rend(); ++it) {
      if (auto f = ic.find(*it); f != ic.end() && f->is_string()) {
        icon = f->get<std::string>();
        found = true;
        break;
      }
    }
    if (!found && ic.contains("default") && ic["default"].is_string()) icon = ic["default"].get<std::string>();
  }

  json vars = data.vars;
  vars["icon"] = icon;
  const std::string format = eff.contains("format") && eff["format"].is_string() ? eff["format"].get<std::string>() : data.format;
  std::string markup = toPango(render(format, vars));

  // A launcher with "logo": true draws the logo instead of a glyph.
  if (m_logo != nullptr) markup.clear();
  if (m_taskbar != nullptr) {
    syncTaskbar(renderer, eff);
    data.shown = !m_taskbar->empty();
  }
  const bool shown = flag(eff, "show", true) && data.shown && (m_type == "spacer" || m_logo != nullptr || m_taskbar != nullptr || !markup.empty());
  auto* rootNode = root();
  if (rootNode != nullptr && (rootNode->visible() != shown || rootNode->participatesInLayout() != shown)) {
    rootNode->setVisible(shown);
    rootNode->setParticipatesInLayout(shown);
    requestUpdate();
  }
  if (!shown) return;

  // The module's opacity eases to its new value.
  if (const float op = static_cast<float>(num(eff, "opacity", 1.0)); op != m_opacity && rootNode != nullptr) {
    const float from = m_opacity;
    m_opacity = op;
    if (m_animations != nullptr && !m_lastMarkup.empty()) {
      m_animations->cancelForOwner(this);
      Node* node = rootNode;
      m_animations->animate(0.0F, 1.0F, 200.0F, Easing::Linear, [node, from, op](float t) { node->setOpacity(from + (op - from) * t); }, {}, this);
    } else {
      rootNode->setOpacity(op);
    }
  }

  // Text look: fg (hoverFg wins while hovered), font, size, weight, italic.
  const ColorSpec barFg = tokenColor(str(m_spec, "_barFg", "text"), colorSpecFromRole(ColorRole::OnSurface));
  std::string fgTok = str(eff, "fg", data.fg);
  if (m_hovered && !str(eff, "hoverFg").empty()) fgTok = str(eff, "hoverFg");
  const ColorSpec fg = tokenColor(fgTok, barFg);
  const double baseSize = num(m_spec, "_barFontSize", 12);
  double size = baseSize;
  if (eff.contains("fontSize")) {
    const auto& fs = eff["fontSize"];
    if (fs.is_string()) {
      const std::string v = fs.get<std::string>();
      try {
        size = (!v.empty() && (v[0] == '+' || v[0] == '-')) ? baseSize + std::stod(v) : (std::stod(v) > 0 ? std::stod(v) : baseSize);
      } catch (...) {
      }
    } else if (fs.is_number() && fs.get<double>() > 0) {
      size = fs.get<double>();
    }
  }
  m_baseFont = static_cast<float>(size);
  // hoverGrow: the font grows while hovered.
  if (const float grow = m_hovered ? static_cast<float>(num(eff, "hoverGrow", 0)) : 0.0F; grow != m_growTarget) {
    m_growTarget = grow;
    if (m_animations != nullptr && !m_lastMarkup.empty()) {
      m_animations->cancelForOwner(m_label);
      const float from = m_grow;
      m_animations->animate(0.0F, 1.0F, 150.0F, Easing::EaseOutCubic, [this, from, grow](float t) {
        m_grow = from + (grow - from) * t;
        requestUpdate();
      }, {}, m_label);
    } else {
      m_grow = grow;
    }
  }
  size += m_grow;
  if (flag(eff, "italic", false)) markup = "<i>" + markup + "</i>";
  const std::string font = str(eff, "font", str(m_spec, "_barFont"));

  // The bar asks every second, so only touch the label (and repaint) when something visible changed.
  const bool bold = flag(eff, "bold", false);
  m_eff = eff;
  const std::string look = (m_hovered ? "h" : "") + eff.value("bg", nlohmann::json()).dump() + markup + "\x1f" + std::to_string(size) + (bold ? "b" : "") + font + "\x1f"
      + std::to_string(fg.role ? static_cast<int>(*fg.role) : -1) + std::to_string(fg.alpha) + std::to_string(fg.fixed.r)
      + std::to_string(fg.fixed.g) + std::to_string(fg.fixed.b);
  // "tooltip": false hides it, a string is a format, otherwise the provider's tooltip shows.
  if (m_area != nullptr) {
    std::string tip;
    const auto t = eff.find("tooltip");
    if (t != eff.end() && t->is_boolean()) {
      tip = t->get<bool>() ? (data.tooltipRich ? data.tooltip : plainTooltip(data.tooltip)) : std::string();
    } else if (t != eff.end() && t->is_string()) {
      tip = toPango(render(t->get<std::string>(), vars));
    } else {
      tip = data.tooltipRich ? data.tooltip : plainTooltip(data.tooltip);
    }
    if (tip != m_lastTip) {
      m_lastTip = tip;
      if (tip.empty()) m_area->clearTooltip();
      else m_area->setTooltip(tip);
    }
  }
  if (look == m_lastMarkup) return;
  m_lastMarkup = look;
  m_label->setColor(fg);
  m_label->setFontSize(static_cast<float>(size) * fontScale());
  m_label->setFontWeight(bold ? FontWeight::Bold : labelFontWeight());
  if (!font.empty()) m_label->setFontFamily(font);
  // Matches the classic line pitch, round(ascent) + round(descent), where Pango would round up. Side bars
  // stack modules by it, and multi-line formats ("{time}\n{date}") are spaced by it.
  {
    const float px = static_cast<float>(size) * fontScale();
    const auto fm = renderer.measureFont(px, bold ? FontWeight::Bold : labelFontWeight(), font.empty() ? labelFontFamily() : font);
    const float pitch = std::round(-fm.top) + std::round(fm.bottom);
    const auto lines = static_cast<float>(std::count(markup.begin(), markup.end(), '\n') + 1);
    m_textHeight = markup.empty() ? 0.0F : lines * pitch;
    m_textAscent = std::round(-fm.top);
    if (lines > 1.0F) {
      markup = "<span line_height=\"" + std::to_string(std::lround(pitch * 1024.0F * renderer.renderScale())) + "\">" + markup + "</span>";
    }
  }
  m_label->setText(markup);
  m_label->measure(renderer);
  requestRedraw();
}

void KusanagiModuleWidget::doLayout(Renderer& renderer, float containerWidth, float containerHeight) {
  auto* rootNode = root();
  if (rootNode == nullptr || m_label == nullptr) return;
  m_vertical = containerHeight > containerWidth;
  update(renderer);
  if (!rootNode->visible()) {
    rootNode->setSize(0.0F, 0.0F);
    return;
  }
  const float s = m_contentScale;
  // Along the bar: gap, cap, padding, content, padding, cap, gap.
  const bool hasText = !m_label->text().empty();
  const float w = hasText ? m_label->width() : 0.0F;
  const float h = hasText ? m_label->height() : 0.0F;
  const float cross = m_vertical ? containerWidth : containerHeight;
  const float edgeIn = (m_groupEdge + m_insetEdge) * s;
  const float innerIn = (m_groupInner + m_insetInner) * s;
  const float boxCross = std::max(0.0F, cross - edgeIn - innerIn);
  const float boxC0 = m_farEdge ? innerIn : edgeIn; // the box's offset across, from the top / left
  // The tooltip anchors on the module across the group's inner thickness.
  if (m_area != nullptr) {
    const float g0 = (m_farEdge ? m_groupInner : m_groupEdge) * s;
    const float g1 = (m_farEdge ? m_groupEdge : m_groupInner) * s;
    m_area->setTooltipAnchorInsets(
        m_vertical ? TooltipAnchorInsets{.top = 0.0F, .right = g1, .bottom = 0.0F, .left = g0}
                   : TooltipAnchorInsets{.top = g0, .right = 0.0F, .bottom = g1, .left = 0.0F}
    );
  }
  const float capS = KusanagiBox::capSize(m_eff.value("capStart", std::string("none")), boxCross);
  const float capE = KusanagiBox::capSize(m_eff.value("capEnd", std::string("none")), boxCross);
  // Side bars turn the text when asked, or (unless rotate: false) when it is wider than the bar.
  const auto rot = m_eff.find("rotate");
  const bool rotTrue = rot != m_eff.end() && rot->is_boolean() && rot->get<bool>();
  const bool rotFalse = rot != m_eff.end() && rot->is_boolean() && !rot->get<bool>();
  const bool turned = m_vertical && hasText && (rotTrue || (!rotFalse && w > boxCross - 2.0F));
  const float labelAlong = !hasText ? 0.0F : m_vertical && !turned ? (m_textHeight > 0.0F ? m_textHeight : h) : w;
  // The launcher logo is a square 1.5 times the font size, after any text.
  const float logoS = m_logo != nullptr ? std::round(m_baseFont * 1.5F) * s : 0.0F;
  // A spacer takes `size` px of room inside the module's padding.
  const float spacerS = m_type == "spacer" ? static_cast<float>(num(m_eff, "size", 8)) * s : 0.0F;
  // Taskbar items are as thick as the module's box.
  if (m_taskbar != nullptr && std::abs(boxCross - m_taskbarCross) > 0.01F) {
    m_taskbarCross = boxCross;
    syncTaskbar(renderer, m_eff);
  }
  const float provS = m_taskbar != nullptr ? (m_vertical ? m_taskbar->height() : m_taskbar->width()) : logoS;
  const float content = labelAlong + provS + spacerS + (labelAlong > 0.0F && provS > 0.0F ? 6.0F * s : 0.0F);
  // Padding comes from the effective spec, since a state may change it.
  float padStart = m_padStart, padEnd = m_padEnd;
  if (const auto p = m_eff.find("padding"); p != m_eff.end()) {
    if (p->is_number()) {
      padStart = padEnd = p->get<float>();
    } else if (p->is_array() && !p->empty() && (*p)[0].is_number()) {
      padStart = (*p)[0].get<float>();
      padEnd = p->size() > 1 && (*p)[1].is_number() ? (*p)[1].get<float>() : padStart;
    }
  }
  const float boxAlong = capS + padStart * s + content + padEnd * s + capE;
  const float along = std::ceil(m_gapStart * s + boxAlong + m_gapEnd * s);
  const float g0 = m_gapStart * s;
  const float lead = g0 + capS + padStart * s;
  m_label->setRotation(turned ? (str(m_spec, "_edge") == "left" ? -1.0F : 1.0F) * std::numbers::pi_v<float> / 2.0F : 0.0F);
  m_label->setTransformOrigin(w / 2.0F, h / 2.0F);
  if (m_vertical) {
    rootNode->setSize(containerWidth, along);
    // Centre the label across the box and in its slot along the bar (a turned label keeps its unrotated
    // box). Stacked text puts its first baseline round(ascent) below a top centred on the slot.
    const float top = turned || m_textHeight <= 0.0F ? std::round(lead + labelAlong / 2.0F - h / 2.0F)
                                                       : std::round(lead + labelAlong / 2.0F - m_textHeight / 2.0F) + m_textAscent - m_label->baselineOffset();
    m_label->setPosition(std::round(boxC0 + boxCross / 2.0F - w / 2.0F), std::round(top));
    m_box.setAnimator(m_animations, [this]() { requestRedraw(); });
    m_box.apply(m_eff, m_hovered, true, boxC0, g0, boxCross, boxAlong, s);
  } else {
    rootNode->setSize(along, containerHeight);
    const float boxY = boxC0;
    // Matches the classic look: text bigger than the bar's font sits 1 px lower.
    const float lower = m_baseFont > static_cast<float>(num(m_spec, "_barFontSize", 12)) ? 1.0F : 0.0F;
    m_label->setPosition(std::round(lead), std::round(boxY + (boxCross - h) / 2.0F + lower));
    m_box.setAnimator(m_animations, [this]() { requestRedraw(); });
    m_box.apply(m_eff, m_hovered, false, g0, boxY, boxAlong, boxCross, s);
  }
  if (m_taskbarNode != nullptr) {
    const float at = lead + labelAlong + (labelAlong > 0.0F ? 6.0F * s : 0.0F);
    if (m_vertical) {
      m_taskbarNode->setPosition(std::round(boxC0 + (boxCross - m_taskbar->width()) / 2.0F), at);
    } else {
      m_taskbarNode->setPosition(at, std::round(boxC0 + (boxCross - m_taskbar->height()) / 2.0F));
    }
  }
  if (m_logo != nullptr) {
    if (!m_logoLoaded) {
      m_logoLoaded = m_logo->setSourceFile(renderer, paths::assetPath("kusanagi-logo.svg").string(), static_cast<int>(logoS * 2.0F), true);
    }
    const float at = lead + labelAlong + (labelAlong > 0.0F ? 6.0F * s : 0.0F);
    m_logo->setSize(logoS, logoS);
    m_logo->setTransformOrigin(logoS / 2.0F, logoS / 2.0F);
    m_logo->setScale(m_hovered ? 1.12F : 1.0F);
    if (m_vertical) {
      m_logo->setPosition(std::round(boxC0 + (boxCross - logoS) / 2.0F), at);
    } else {
      m_logo->setPosition(at, std::round(boxC0 + (boxCross - logoS) / 2.0F));
    }
  }
}

// When-state actions

std::optional<kusanagi::bar::WidgetAction> KusanagiModuleWidget::gestureOverride(kusanagi::bar::Gesture gesture) {
  // The translator writes _stateActions as {state: {gesture: action}} ("alt" is the module's own toggle),
  // and _stateBase as {gesture: action outside those states} for gestures only a state binds.
  const auto acts = m_spec.find("_stateActions");
  if (acts == m_spec.end() || !acts->is_object()) return std::nullopt;
  const std::string key(kusanagi::bar::gestureConfigKey(gesture));
  std::optional<std::string> chosen;
  // The last state that sets the gesture wins.
  for (const auto& st : m_states) {
    if (const auto it = acts->find(st); it != acts->end() && it->is_object() && it->contains(key) && (*it)[key].is_string()) {
      chosen = (*it)[key].get<std::string>();
    }
  }
  if (!chosen) {
    const auto base = m_spec.find("_stateBase");
    if (base == m_spec.end() || !base->is_object() || !base->contains(key)) return std::nullopt; // the binding
    chosen = (*base)[key].is_string() ? (*base)[key].get<std::string>() : std::string("none");
  }
  if (*chosen == "alt") {
    m_altOn = !m_altOn;
    m_lastMarkup.clear();
    requestUpdate();
    return kusanagi::bar::WidgetAction{};
  }
  auto parsed = kusanagi::bar::parseWidgetAction(*chosen);
  if (!parsed) return kusanagi::bar::WidgetAction{};
  return *parsed;
}

void KusanagiModuleWidget::onGestureDispatch(kusanagi::bar::Gesture gesture, const kusanagi::bar::WidgetAction& action) {
  (void)gesture;
  (void)action;
  // "refresh": true re-runs a custom command right after a click or scroll.
  if (m_type != "custom" || m_customStream || !flag(m_eff, "refresh", false)) return;
  std::weak_ptr<int> alive = m_alive;
  DeferredCall::callLater([this, alive]() {
    if (alive.lock()) customKick();
  });
}

// Custom modules

void KusanagiModuleWidget::customSync(const json& eff) {
  const std::string cmd = str(eff, "exec");
  const bool stream = flag(eff, "stream", false);
  const double every = num(eff, "interval", 5.0);
  const auto* gm = GameModeService::instance();
  const bool paused = gm != nullptr && gm->quiet() && !flag(eff, "game", false);
  if (cmd != m_customCmd || stream != m_customStream) {
    // The command changed: stop the old one and start the new one.
    customStop();
    m_customCmd = cmd;
    m_customStream = stream;
    m_customPaused = paused;
    m_customEvery = -1.0;
    customKick();
  } else if (paused != m_customPaused) {
    m_customPaused = paused;
    if (paused) {
      if (stream) customStop();
    } else {
      customKick();
    }
  } else if (stream && !paused && !m_customRunning && !m_customRestart.active()) {
    customKick(); // a stream stopped while paused comes back
  }
  // One-shot commands re-run every `interval` seconds (0 = once), never while the last run is still going.
  const bool timed = !stream && every > 0.0 && !cmd.empty() && !paused;
  if (!timed) {
    m_customTimer.stop();
    m_customEvery = -1.0;
  } else if (every != m_customEvery || !m_customTimer.active()) {
    m_customEvery = every;
    std::weak_ptr<int> alive = m_alive;
    m_customTimer.startRepeating(std::chrono::milliseconds(std::max<long long>(250, std::llround(every * 1000.0))), [this, alive]() {
      if (alive.lock() && !m_customRunning) customKick();
    });
  }
}

void KusanagiModuleWidget::customKick() {
  if (m_customCmd.empty() || m_customPaused || m_customRunning) return;
  auto run = std::make_shared<CustomRun>();
  m_customRun = run;
  m_customRunning = true;
  const std::uint64_t gen = ++m_customGen;
  const bool stream = m_customStream;
  std::weak_ptr<int> alive = m_alive;
  // The wrapper prints its pid (the process group) first, so the module can kill it from the main thread too.
  const std::vector<std::string> args{"sh", "-c", "printf '\\001%s\\n' $$ >&2; exec sh -c \"$1\"", "sh", m_customCmd};
  auto errHead = std::make_shared<std::string>();
  auto lineBuf = std::make_shared<std::string>();
  process::RunCallbacks callbacks;
  callbacks.stdErr = [run, errHead](std::string_view chunk) {
    if (run->pgid.load() != 0 || errHead->size() > 64) return;
    errHead->append(chunk);
    if (errHead->size() > 1 && (*errHead)[0] == '\x01') {
      if (const auto nl = errHead->find('\n'); nl != std::string::npos) {
        try {
          run->pgid = std::stoi(errHead->substr(1, nl - 1));
        } catch (...) {
        }
      }
    }
  };
  if (stream) {
    // Streams update on every line.
    callbacks.stdOut = [this, alive, gen, lineBuf](std::string_view chunk) {
      lineBuf->append(chunk);
      for (auto nl = lineBuf->find('\n'); nl != std::string::npos; nl = lineBuf->find('\n')) {
        std::string line = lineBuf->substr(0, nl);
        lineBuf->erase(0, nl + 1);
        DeferredCall::callLater([this, alive, gen, line = std::move(line)]() {
          if (alive.lock() && gen == m_customGen) customTake(line);
        });
      }
    };
  }
  callbacks.onExit = [this, alive, gen, stream, run](process::RunResult r) {
    run->exited = true;
    DeferredCall::callLater([this, alive, gen, stream, out = std::move(r.out)]() {
      if (!alive.lock() || gen != m_customGen) return;
      m_customRunning = false;
      m_customRun.reset();
      if (!stream) {
        // One-shot: the last non-empty line wins.
        std::string last;
        std::istringstream in(out);
        for (std::string l; std::getline(in, l);) {
          if (!trimmed(l).empty()) last = l;
        }
        customTake(last);
      } else if (!m_customPaused) {
        // A stream that ended comes back 2 s later.
        std::weak_ptr<int> again = m_alive;
        m_customRestart.start(std::chrono::seconds(2), [this, again]() {
          if (again.lock()) customKick();
        });
      }
    });
  };
  process::RunOptions options;
  options.cancel = run->cancel;
  options.maxOutputBytes = stream ? 0 : (1U << 20);
  if (!process::runAsync(args, std::move(callbacks), options)) {
    m_customRunning = false;
    m_customRun.reset();
  }
}

void KusanagiModuleWidget::customStop() {
  m_customRestart.stop();
  if (m_customRun != nullptr) {
    m_customRun->cancel->store(true);
    // The worker kills the group within 250 ms, but at teardown the shell may be gone by then.
    if (const int pg = m_customRun->pgid.load(); pg > 0 && !m_customRun->exited.load()) ::kill(-pg, SIGTERM);
    m_customRun.reset();
  }
  m_customRunning = false;
  ++m_customGen; // whatever the old run still says is ignored
}

void KusanagiModuleWidget::customTake(const std::string& line) {
  const std::string t = trimmed(line);
  json next = json{{"text", ""}};
  bool parsed = false;
  if (!t.empty() && t[0] == '{') {
    json j = json::parse(t, nullptr, false);
    if (!j.is_discarded() && j.is_object()) {
      for (auto it = j.begin(); it != j.end(); ++it) next[it.key()] = it.value();
      parsed = true;
    }
  }
  if (!parsed) next = json{{"text", t}};
  if (next == m_customData) return;
  m_customData = std::move(next);
  m_lastMarkup.clear();
  requestUpdate();
}

// Taskbar

void KusanagiModuleWidget::syncTaskbar(Renderer& renderer, const json& eff) {
  if (m_taskbar == nullptr) return;
  const double base = num(m_spec, "_barFontSize", 12);
  double size = base;
  if (const auto fs = eff.find("fontSize"); fs != eff.end()) {
    if (fs->is_string()) {
      try {
        const std::string v = fs->get<std::string>();
        size = !v.empty() && (v[0] == '+' || v[0] == '-') ? base + std::stod(v) : std::stod(v) > 0 ? std::stod(v) : base;
      } catch (...) {
      }
    } else if (fs->is_number() && fs->get<double>() > 0) {
      size = fs->get<double>();
    }
  }
  const float s = m_contentScale;
  // Before the first layout the thickness inside the insets isn't known yet, so guess; layout re-syncs.
  const float cross = m_taskbarCross > 0.0F ? m_taskbarCross : 22.0F * s;
  const bool changed = m_taskbar->sync(
      renderer,
      KusanagiTaskbar::Look{
          .eff = eff,
          .vertical = m_vertical,
          .edge = str(m_spec, "_edge", "top"),
          .cross = cross,
          .baseFont = static_cast<float>(size),
          .font = !str(eff, "font", str(m_spec, "_barFont")).empty() ? str(eff, "font", str(m_spec, "_barFont")) : labelFontFamily(),
          .scale = s,
          .fontScale = fontScale(),
          .fontWeight = static_cast<int>(FontWeight::Normal), // module labels use the bar's weight instead
      },
      m_animations
  );
  if (changed) requestRedraw();
}
