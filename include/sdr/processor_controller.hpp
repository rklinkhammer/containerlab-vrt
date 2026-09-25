#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>

namespace sdr {
struct ProcessorRadioControlConfig {
  std::string id;
  std::uint32_t sid{};
  std::string control_host;
  std::uint16_t control_port{};
  std::string status_host;
  std::uint16_t status_port{};
  std::uint64_t center_hz{};
  std::uint32_t sample_rate_hz{};
  std::uint32_t bandwidth_hz{};
  std::int16_t gain_db_q7{};
};

struct ProcessorControllerConfig {
  std::array<ProcessorRadioControlConfig, 4> radios;
  std::size_t queue_limit{64};
  std::size_t retry_limit{8};
  std::chrono::milliseconds io_timeout{std::chrono::seconds(2)};
  std::chrono::milliseconds maximum_backoff{std::chrono::seconds(2)};
  std::chrono::milliseconds liveness_interval{std::chrono::seconds(1)};
};

struct ProcessorControllerMetrics {
  std::uint64_t connection_attempts{};
  std::uint64_t connections{};
  std::uint64_t reconnects{};
  std::uint64_t status_failures{};
  std::uint64_t protocol_failures{};
  std::uint64_t configurations{};
  std::uint64_t starts_submitted{};
  std::uint64_t starts_admitted{};
  std::uint64_t stale_starts_replayed{};
  std::uint64_t boot_changes{};
};

class ProcessorController {
public:
  explicit ProcessorController(ProcessorControllerConfig config,
                               std::ostream &events);
  ~ProcessorController();
  ProcessorController(const ProcessorController &) = delete;
  ProcessorController &operator=(const ProcessorController &) = delete;

  ProcessorControllerMetrics metrics() const noexcept;
  bool coordinated() const noexcept;

private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};
} // namespace sdr