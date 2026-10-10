#pragma once

#include "config/config_types.h"
#include "theme/builtin_templates.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace kusanagi::theme {

  class CommunityTemplateService {
  public:
    // Templates already present in the local community template directory. Nothing is downloaded.
    [[nodiscard]] static std::vector<AvailableTemplate> availableTemplates();
  };

  [[nodiscard]] std::filesystem::path communityTemplatesCacheDir();
  [[nodiscard]] std::filesystem::path communityTemplateDir(std::string_view id);
  [[nodiscard]] std::filesystem::path communityTemplateConfigPath(std::string_view id);
  [[nodiscard]] bool isSafeCommunityTemplateId(std::string_view id);

} // namespace kusanagi::theme
