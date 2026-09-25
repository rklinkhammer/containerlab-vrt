#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sdr {
inline constexpr std::size_t maximum_status_request_bytes = 1024;
inline constexpr std::size_t maximum_status_response_bytes = 4096;

class StatusFrameDecoder {
public:
  bool feed(std::span<const std::byte> input);
  bool complete() const noexcept { return complete_; }
  bool failed() const noexcept { return failed_; }
  std::size_t expected_payload_bytes() const noexcept { return expected_; }
  nlohmann::json take_request();

private:
  std::array<std::byte, 4> header_{};
  std::array<std::byte, maximum_status_request_bytes> payload_{};
  std::size_t header_bytes_{};
  std::size_t payload_bytes_{};
  std::size_t expected_{};
  bool complete_{};
  bool failed_{};
};

std::vector<std::byte> encode_status_response(const nlohmann::json &response);

struct ControlSnapshot {
  bool occupied{};
  std::optional<std::uint64_t> generation;
};

class ControlSlot {
public:
  using Clock = std::chrono::steady_clock;
  std::optional<std::uint64_t> acquire(Clock::time_point now) noexcept;
  bool observe_liveness(std::uint64_t generation, Clock::time_point now) noexcept;
  bool release(std::uint64_t generation) noexcept;
  bool release_if_stale(Clock::time_point now,
                        std::chrono::milliseconds timeout) noexcept;
  ControlSnapshot snapshot() const noexcept;

private:
  mutable std::mutex mutex_;
  std::uint64_t next_generation_{1};
  std::optional<std::uint64_t> active_generation_;
  Clock::time_point last_liveness_{};
};
} // namespace sdr
