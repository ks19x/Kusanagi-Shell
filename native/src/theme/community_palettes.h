#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace kusanagi::theme {

  struct AvailablePalette {
    struct PreviewMode {
      std::string surface;
      std::vector<std::string> accents;
    };

    struct Preview {
      PreviewMode dark;
      PreviewMode light;
    };

    std::string name;
    std::string md5;
    Preview preview = {};
  };

  [[nodiscard]] std::vector<AvailablePalette> availableCommunityPalettes();

  // Catalog-declared MD5 of the named palette's downloaded JSON, or empty if the
  // palette is absent from the cached catalog or the catalog omits a checksum.
  [[nodiscard]] std::string communityPaletteCatalogMd5(std::string_view name);

  [[nodiscard]] std::filesystem::path communityPaletteCacheDir();
  [[nodiscard]] std::filesystem::path communityPaletteCachePath(std::string_view name);

} // namespace kusanagi::theme
