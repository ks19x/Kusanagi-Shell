#include "shell/kusanagi/kusanagi_style.h"

#include "util/file_utils.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <map>

namespace kusanagi {

  namespace {
    nlohmann::json& store() {
      static nlohmann::json s = nlohmann::json::object();
      return s;
    }
  } // namespace

  const nlohmann::json& settings() { return store(); }

  void setSettings(nlohmann::json s) { store() = s.is_object() ? std::move(s) : nlohmann::json::object(); }

  bool setOption(const char* section, const char* key, const nlohmann::json& value) {
    const std::filesystem::path path = std::filesystem::path(FileUtils::configDir()) / "settings.json";
    nlohmann::json s = nlohmann::json::object();
    {
      std::ifstream in(path);
      if (in) {
        s = nlohmann::json::parse(in, nullptr, false);
        if (!s.is_object()) return false; // never clobber a file we couldn't read
      }
    }
    s[section][key] = value;
    const auto tmp = path.string() + ".tmp";
    {
      std::ofstream out(tmp);
      if (!out) return false;
      out << s.dump(4) << "\n";
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) return false;
    store() = std::move(s);
    return true;
  }

  bool editSettings(const std::function<void(nlohmann::json&)>& edit) {
    const std::filesystem::path path = std::filesystem::path(FileUtils::configDir()) / "settings.json";
    nlohmann::json s = nlohmann::json::object();
    {
      std::ifstream in(path);
      if (in) {
        s = nlohmann::json::parse(in, nullptr, false);
        if (!s.is_object()) return false; // never clobber a file we couldn't read
      }
    }
    edit(s);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = path.string() + ".tmp";
    {
      std::ofstream out(tmp);
      if (!out) return false;
      out << s.dump(4) << "\n";
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) return false;
    store() = std::move(s);
    return true;
  }

  ColorSpec alpha(ColorRole role, float a) { return colorSpecFromRole(role, a); }

  ColorSpec color(const std::string& tokenIn, const ColorSpec& fallback) {
    if (tokenIn.empty()) return fallback;
    std::string token = tokenIn;
    float a = 1.0F;
    if (const auto slash = token.rfind('/'); slash != std::string::npos && slash > 0) {
      try {
        a = std::stof(token.substr(slash + 1));
      } catch (...) {
      }
      token = token.substr(0, slash);
    }
    if (token == "transparent" || token == "none") return clearColorSpec();
    static const std::map<std::string, ColorRole> roles{
        {"accent", ColorRole::Primary},       {"accent2", ColorRole::Secondary},
        {"text", ColorRole::OnSurface},       {"fg", ColorRole::OnSurface},
        {"dim", ColorRole::OnSurfaceVariant}, {"faint", ColorRole::Outline},
        {"bg", ColorRole::Surface},           {"panel", ColorRole::Surface},
        {"card", ColorRole::SurfaceVariant},  {"border", ColorRole::SurfaceVariant},
        {"danger", ColorRole::Error},         {"ok", ColorRole::Tertiary},
    };
    if (const auto it = roles.find(token); it != roles.end()) return colorSpecFromRole(it->second, a);
    if (token == "warn") return ColorSpec{.role = std::nullopt, .fixed = hex("#e8be62"), .alpha = a};
    if (!token.empty() && token[0] == '#') return ColorSpec{.role = std::nullopt, .fixed = hex(token), .alpha = a};
    return fallback;
  }

  int ms(int base) {
    const double speed = opt<double>("look", "animSpeed", 1.0);
    return std::max(1, static_cast<int>(std::lround(base * speed)));
  }

  float bounce(float base) { return base * static_cast<float>(opt<double>("look", "bounce", 1.0)); }

  float radius() { return static_cast<float>(opt<double>("look", "radius", 16.0)); }

  std::string font() { return opt<std::string>("look", "font", "JetBrainsMono Nerd Font"); }

  ColorSpec surfaceBorder() {
    return opt<bool>("look", "borderAccent", false) ? colorSpecFromRole(ColorRole::Primary, 0.55F)
                                                    : colorSpecFromRole(ColorRole::OnSurface, 0.08F);
  }

  float surfaceBorderWidth() { return opt<bool>("look", "borders", true) ? 1.0F : 0.0F; }

  bool shadows() { return opt<bool>("look", "shadows", true); }

} // namespace kusanagi
