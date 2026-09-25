#pragma once

#include <cstdint>

namespace sdr {
struct ProtocolTime {
  std::uint64_t seconds{};
  std::uint64_t picoseconds{};
  auto operator<=>(const ProtocolTime &) const = default;
};
} // namespace sdr
