#pragma once

#include <string>

namespace kusanagi::launcher {

  struct DmenuSocketPathResult {
    std::string path;
    std::string error;
  };

  [[nodiscard]] DmenuSocketPathResult resolveDmenuSocketPath();

} // namespace kusanagi::launcher
