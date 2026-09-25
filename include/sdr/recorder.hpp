#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace sdr::recorder {
inline constexpr auto maximum_duration = std::chrono::seconds(60);
inline constexpr std::uint64_t maximum_capture_bytes = 64ULL * 1024 * 1024;
inline constexpr std::size_t receive_buffer_bytes = 65'535;
inline constexpr int socket_receive_bytes = 4 * 1024 * 1024;

struct Options {
  std::string interface;
  std::filesystem::path metrics_path;
  std::optional<std::filesystem::path> pcap_path;
  std::optional<std::chrono::seconds> duration;
  std::uint64_t capture_bytes{maximum_capture_bytes};
};

struct Metrics {
  std::uint64_t frames{};
  std::uint64_t wire_bytes{};
  std::uint64_t receive_truncations{};
  std::uint64_t receive_errors{};
  std::uint64_t kernel_packets{};
  std::uint64_t kernel_drops{};
  std::uint64_t pcap_frames{};
  std::uint64_t pcap_bytes{};
  std::uint64_t pcap_io_errors{};
  bool pcap_limit_reached{};

  void observe_frame(std::size_t wire_size, std::size_t copied_size) noexcept;
  void observe_kernel(std::uint64_t packets, std::uint64_t drops) noexcept;
};

class PcapWriter {
public:
  PcapWriter(const std::filesystem::path &path, std::uint64_t byte_limit);
  ~PcapWriter();
  PcapWriter(const PcapWriter &) = delete;
  PcapWriter &operator=(const PcapWriter &) = delete;

  bool write(std::span<const std::byte> frame, std::size_t wire_size,
             std::chrono::system_clock::time_point timestamp);
  std::uint64_t bytes() const noexcept { return bytes_; }
  bool limit_reached() const noexcept { return limit_reached_; }
  void finish();

private:
  struct State;
  State *state_;
  std::uint64_t byte_limit_;
  std::uint64_t bytes_{};
  bool limit_reached_{};
};

Options parse_options(int argc, char **argv);
nlohmann::json metrics_json(const Options &options, const Metrics &metrics,
                            std::chrono::milliseconds uptime,
                            std::int64_t observed_unix_ns,
                            const std::string &status);
void write_metrics(const Options &options, const Metrics &metrics,
                   std::chrono::steady_clock::time_point started,
                   const std::string &status);
} // namespace sdr::recorder