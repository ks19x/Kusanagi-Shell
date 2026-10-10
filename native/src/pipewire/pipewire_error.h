#pragma once

#include <cstdint>

namespace kusanagi::pipewire {

  enum class ErrorDisposition : std::uint8_t {
    Reconnect,
    StaleObject,
    Report,
  };

  [[nodiscard]] ErrorDisposition classifyError(std::uint32_t objectId, int result) noexcept;

} // namespace kusanagi::pipewire
