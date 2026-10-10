#pragma once

// Kusanagi's look for native surfaces: the live settings.json, colour tokens and motion.
// Colour tokens map onto the palette as accent = primary, accent2 = secondary, ok = tertiary, danger = error,
// panel = surface, card = surface_variant, text = on_surface, dim = on_surface_variant, faint = outline.

#include "ui/palette.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace kusanagi {

  // The parsed ~/.config/kusanagi/settings.json as of the last config load, or an empty object.
  [[nodiscard]] const nlohmann::json& settings();
  void setSettings(nlohmann::json settings); // called by config/kusanagi_import.cpp at every load
  // Changes one option in settings.json with an atomic write. The config reload picks it up like a hand edit.
  bool setOption(const char* section, const char* key, const nlohmann::json& value);
  // Changes several options in one atomic write. `edit` gets the file as read from disk (an empty object when
  // there is none) and changes it in place. False when the file couldn't be read or written.
  bool editSettings(const std::function<void(nlohmann::json&)>& edit);

  template <typename T> [[nodiscard]] T opt(const char* section, const char* key, T fallback) {
    const auto& s = settings();
    const auto sec = s.find(section);
    if (sec == s.end() || !sec->is_object()) return fallback;
    const auto it = sec->find(key);
    if (it == sec->end() || it->is_null()) return fallback;
    try {
      return it->get<T>();
    } catch (...) {
      return fallback;
    }
  }

  // A colour token like "accent", "bg/0.7" or "#ff0000" as a ColorSpec that follows the live palette.
  [[nodiscard]] ColorSpec color(const std::string& token, const ColorSpec& fallback = colorSpecFromRole(ColorRole::OnSurface));
  [[nodiscard]] ColorSpec alpha(ColorRole role, float a);

  // A duration scaled by look.animSpeed. With animations off it is 1 ms.
  [[nodiscard]] int ms(int base);
  // An OutBack overshoot scaled by look.bounce.
  [[nodiscard]] float bounce(float base);
  [[nodiscard]] float radius();
  [[nodiscard]] std::string font();
  // Text colour at 8%, or the accent at 55% with look.borderAccent. The width is 0 when look.borders is off.
  [[nodiscard]] ColorSpec surfaceBorder();
  [[nodiscard]] float surfaceBorderWidth();
  [[nodiscard]] bool shadows();

} // namespace kusanagi
