#include "core/build_info.h"

#include "kusanagi_git_revision.h"

namespace kusanagi::build_info {

  std::string_view version() noexcept { return KUSANAGI_VERSION; }

  std::string_view revision() noexcept { return KUSANAGI_GIT_REVISION; }

  std::string displayVersion() {
    std::string label{version()};

    constexpr std::string_view unknownRevision = "unknown";
    const std::string_view rev = revision();
    if (!rev.empty() && rev != unknownRevision) {
      label += " (";
      label += rev;
      label += ')';
    }
    return label;
  }

} // namespace kusanagi::build_info
