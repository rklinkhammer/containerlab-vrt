#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <sdr/processor_controller.hpp>
#include <sdr/spectrum.hpp>

namespace sdr::udp {
struct Endpoint {
  std::string host;
  std::uint16_t port{};
};

struct Listener {
  std::uint32_t stream_id{};
  Endpoint endpoint;
};

struct ProcessorConfig {
  std::array<Listener, 4> listeners;
  std::string source_host;
  Endpoint destination;
  ProcessingConfig processing;
  std::size_t receive_bytes{};
  std::size_t queue_limit{};
  std::optional<ProcessorControllerConfig> controller;
};

struct DetectorConfig {
  Endpoint listener;
  std::uint32_t fft_size{};
  std::size_t receive_bytes{};
};

struct ProcessorMetrics {
  std::uint64_t datagrams{};
  std::uint64_t malformed{};
  std::uint64_t context_updates{};
  std::uint64_t packet_gaps{};
  std::uint64_t spectra{};
  std::uint64_t send_failures{};
  std::uint64_t unavailable{};
};

struct DetectorMetrics {
  std::uint64_t datagrams{};
  std::uint64_t detections{};
  std::uint64_t malformed{};
  std::uint64_t sequence_gaps{};
  std::uint64_t gapped{};
  std::uint64_t zero{};
  std::uint64_t unavailable{};
};

struct Detection {
  std::uint32_t stream_id{};
  ProtocolTime observation_time{};
  std::optional<double> frequency_hz;
  std::string validity;
};

ProcessorConfig load_processor_config(const std::string &path);
DetectorConfig load_detector_config(const std::string &path);

class ProcessorCore {
public:
  explicit ProcessorCore(ProcessorConfig config);
  ~ProcessorCore();
  ProcessorCore(const ProcessorCore &) = delete;
  ProcessorCore &operator=(const ProcessorCore &) = delete;
  std::vector<std::vector<std::byte>>
  consume(std::uint32_t listener_stream_id, std::span<const std::byte> datagram);
  void note_send_failure() noexcept { ++metrics_.send_failures; }
  const ProcessorMetrics &metrics() const noexcept { return metrics_; }

private:
  struct StreamState;
  ProcessorConfig config_;
  std::array<StreamState, 4> *streams_;
  ProcessorMetrics metrics_;
};

class DetectorCore {
public:
  explicit DetectorCore(DetectorConfig config) : config_(std::move(config)) {}
  std::optional<Detection> consume(std::span<const std::byte> datagram);
  void note_unavailable() noexcept { ++metrics_.unavailable; }
  void finish();
  const DetectorMetrics &metrics() const noexcept { return metrics_; }

private:
  DetectorConfig config_;
  DetectorMetrics metrics_;
  std::array<bool, 4> seen_{};
  std::array<std::optional<std::uint32_t>, 4> sequences_{};
};

int run_processor(const ProcessorConfig &config,
                  std::optional<std::chrono::milliseconds> duration,
                  std::ostream &output,
                  bool (*stop_requested)() noexcept = nullptr);
int run_detector(const DetectorConfig &config,
                 std::optional<std::chrono::milliseconds> duration,
                 std::ostream &output,
                 bool (*stop_requested)() noexcept = nullptr);
std::string detection_json(const Detection &detection);
std::string metrics_json(const ProcessorMetrics &metrics);
std::string metrics_json(const DetectorMetrics &metrics);
std::optional<std::chrono::milliseconds> parse_duration(int argc, char **argv,
                                                        std::string &config_path);
} // namespace sdr::udp