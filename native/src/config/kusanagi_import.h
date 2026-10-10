#pragma once

// Turns ~/.config/kusanagi/settings.json, colors.json and the `wallpaper` file into a base TOML table on
// every config load. Real *.toml files and the settings.toml sidecar override it. The palette and accent
// are written to <config>/palettes/kusanagi.json as a custom palette.

#include <filesystem>
#include <string>
#include <vector>

#include <toml++/toml.hpp>

namespace kusanagi::config {

  struct KusanagiImport {
    toml::table table;                        // Empty when there's no settings.json.
    std::vector<std::filesystem::path> files; // Files that were read, watched for changes.
  };

  // Files in the config dir whose change means "reload" (besides *.toml).
  [[nodiscard]] bool isKusanagiSourceFile(std::string_view name);

  [[nodiscard]] KusanagiImport importKusanagiSettings(const std::filesystem::path& configDir);

  // Loads only the look (kusanagi::settings() and the UI palette), without the config pipeline and without
  // writing anything. Used by `kusanagi-shell --greeter`, whose home is read-only. False without settings.json.
  bool loadKusanagiLook(const std::filesystem::path& configDir);

} // namespace kusanagi::config
