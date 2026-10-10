#pragma once

// Ready-made bar layouts for settings.json "bars" (the bar engine spec, see docs/bar.md). Used by the bar
// settings page, the presets and the `bar-layout` IPC target. A template is only a starting point: apply it,
// then edit anything.

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace kusanagi::bar_templates {

  struct Template {
    std::string id;
    std::string name;
    std::string note;
    nlohmann::json bars; // array of bar specs
  };

  // Every template, in the order the settings page lists them.
  [[nodiscard]] const std::vector<Template>& list();
  [[nodiscard]] const Template* find(std::string_view id);
  // A copy of the template's "bars" array, empty for an unknown id.
  [[nodiscard]] nlohmann::json bars(std::string_view id);
  // Writes the template to settings.json. False for an unknown id or a failed write.
  bool apply(std::string_view id);
  // Clears "bars", which brings back the classic bar.
  bool classic();
  // Space-separated ids, for `bar-layout list`.
  [[nodiscard]] std::string ids();

} // namespace kusanagi::bar_templates
