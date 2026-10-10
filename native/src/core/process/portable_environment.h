#pragma once

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>

namespace process::portable {

  inline constexpr std::array variables{
      "PIPEWIRE_MODULE_DIR", "SPA_PLUGIN_DIR", "PIPEWIRE_CONFIG_DIR", "WIREPLUMBER_MODULE_DIR"
  };

  // Bundle-specific plugin paths must not leak into applications launched by the shell.
  inline bool omitFromHostEnvironment(std::string_view name) {
    if (std::getenv("KUSANAGI_PORTABLE") == nullptr)
      return false;
    if (name == "KUSANAGI_PORTABLE" || name.starts_with("KUSANAGI_HOST_"))
      return true;
    for (const char* variable : variables) {
      if (name == variable)
        return std::getenv((std::string("KUSANAGI_HOST_") + variable).c_str()) == nullptr;
    }
    return false;
  }

  inline void restoreHostEnvironment() {
    if (std::getenv("KUSANAGI_PORTABLE") == nullptr)
      return;
    for (const char* variable : variables) {
      const std::string saved = std::string("KUSANAGI_HOST_") + variable;
      if (const char* value = std::getenv(saved.c_str()))
        ::setenv(variable, value, 1);
      else
        ::unsetenv(variable);
      ::unsetenv(saved.c_str());
    }
    ::unsetenv("KUSANAGI_PORTABLE");
  }

} // namespace process::portable
