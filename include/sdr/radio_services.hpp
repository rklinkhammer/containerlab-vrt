#pragma once

#include <vita/runtime/transport/stream_framer.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

#include <nlohmann/json.hpp>

#include <sdr/status.hpp>
#include <sdr/virtual_radio.hpp>

namespace sdr {
struct RadioConfig {
  std::string id;
  std::string application_host;
  std::string data_source_host;
  std::size_t application_mtu{};
  std::uint32_t sid{};
  std::uint64_t signal_hz{};
  double amplitude{};
  double phase_radians{};
  RadioSettings defaults;
  std::string iq_host;
  std::uint16_t iq_port{};
  std::uint16_t control_port{};
  std::string status_bind;
  std::uint16_t status_port{};
  std::size_t control_queue{};
  std::size_t status_connections{};
  std::size_t samples_per_packet{};
  std::size_t burst_samples{};
};

RadioConfig load_radio_config(const std::string &path);
nlohmann::json radio_status(const RadioConfig &config,
                            const std::string &boot_id,
                            const ControlSnapshot &control,
                            const RadioSettings &settings,
                            std::uint64_t sample_ordinal,
                            std::uint64_t clipped_samples, bool streaming,
                            std::uint64_t uptime_ms,
                            std::uint64_t observation_unix_ns, bool ready);

bool serve_status_transaction(
    int descriptor, const nlohmann::json &status,
    std::chrono::milliseconds timeout = std::chrono::seconds(2)) noexcept;

class VrtControlFramer {
public:
  using PacketHandler = std::function<bool(std::span<const std::byte>)>;

  VrtControlFramer(std::size_t maximum_packet_bytes,
                   std::size_t packet_limit, PacketHandler handler);
  bool feed(std::span<const std::byte> input) noexcept;
  bool disconnect() noexcept;
  std::size_t packets() const noexcept { return packets_; }
  bool stalled() const noexcept { return framer_.stalled(); }

private:
  static vita::Result<void> deliver(void *context, vita::Bytes packet) noexcept;

  vita::runtime::transport::StreamFramer<65'536> framer_;
  std::size_t packet_limit_;
  std::size_t packets_{};
  PacketHandler handler_;
};
} // namespace sdr