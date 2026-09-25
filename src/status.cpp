#include <sdr/status.hpp>

#include <algorithm>
#include <stdexcept>

namespace sdr {
bool StatusFrameDecoder::feed(std::span<const std::byte> input) {
  if (failed_ || complete_) {
    failed_ = true;
    return false;
  }
  while (!input.empty() && !failed_ && !complete_) {
    if (header_bytes_ < header_.size()) {
      const auto count = std::min(input.size(), header_.size() - header_bytes_);
      std::copy_n(input.begin(), count, header_.begin() + header_bytes_);
      header_bytes_ += count;
      input = input.subspan(count);
      if (header_bytes_ != header_.size())
        continue;
      expected_ = 0;
      for (std::byte value : header_)
        expected_ = (expected_ << 8) | std::to_integer<unsigned>(value);
      if (expected_ == 0 || expected_ > maximum_status_request_bytes) {
        failed_ = true;
        return false;
      }
    }
    const auto count = std::min(input.size(), expected_ - payload_bytes_);
    std::copy_n(input.begin(), count, payload_.begin() + payload_bytes_);
    payload_bytes_ += count;
    input = input.subspan(count);
    complete_ = payload_bytes_ == expected_;
    if (complete_ && !input.empty()) {
      failed_ = true;
      complete_ = false;
    }
  }
  return !failed_;
}

nlohmann::json StatusFrameDecoder::take_request() {
  if (!complete_ || failed_)
    throw std::logic_error("status request is incomplete");
  auto request = nlohmann::json::parse(
      reinterpret_cast<const char *>(payload_.data()),
      reinterpret_cast<const char *>(payload_.data() + payload_bytes_));
  if (!request.is_object() || request.size() != 2 ||
      request.value("version", 0) != 1 ||
      request.value("operation", "") != "get_status")
    throw std::invalid_argument("unsupported status request");
  complete_ = false;
  return request;
}

std::vector<std::byte> encode_status_response(const nlohmann::json &response) {
  const auto payload = response.dump();
  if (payload.empty() || payload.size() > maximum_status_response_bytes)
    throw std::length_error("status response exceeds configured bound");
  std::vector<std::byte> wire(payload.size() + 4);
  for (std::size_t index = 0; index < 4; ++index)
    wire[index] = std::byte((payload.size() >> (8 * (3 - index))) & 0xff);
  std::transform(payload.begin(), payload.end(), wire.begin() + 4,
                 [](char value) { return std::byte{static_cast<unsigned char>(value)}; });
  return wire;
}

std::optional<std::uint64_t>
ControlSlot::acquire(Clock::time_point now) noexcept {
  std::lock_guard lock(mutex_);
  if (active_generation_ || next_generation_ == 0)
    return std::nullopt;
  active_generation_ = next_generation_++;
  last_liveness_ = now;
  return active_generation_;
}

bool ControlSlot::observe_liveness(std::uint64_t generation,
                                   Clock::time_point now) noexcept {
  std::lock_guard lock(mutex_);
  if (active_generation_ != generation || now < last_liveness_)
    return false;
  last_liveness_ = now;
  return true;
}

bool ControlSlot::release(std::uint64_t generation) noexcept {
  std::lock_guard lock(mutex_);
  if (active_generation_ != generation)
    return false;
  active_generation_.reset();
  return true;
}

bool ControlSlot::release_if_stale(Clock::time_point now,
                                   std::chrono::milliseconds timeout) noexcept {
  std::lock_guard lock(mutex_);
  if (!active_generation_ || now < last_liveness_ || now - last_liveness_ < timeout)
    return false;
  active_generation_.reset();
  return true;
}

ControlSnapshot ControlSlot::snapshot() const noexcept {
  std::lock_guard lock(mutex_);
  return {.occupied = active_generation_.has_value(),
          .generation = active_generation_};
}
} // namespace sdr
