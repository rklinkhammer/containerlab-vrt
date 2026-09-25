#include <sdr/recorder.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace sdr::recorder {
namespace {
constexpr std::uint64_t pcap_header_bytes = 24;
constexpr std::uint64_t pcap_record_header_bytes = 16;
constexpr std::string_view usage =
    "usage: recorder --interface INTERFACE --metrics PATH "
    "[--duration-seconds 1..60] [--pcap PATH "
    "[--capture-bytes 40..67108864]]";

std::uint64_t parse_number(std::string_view value, std::uint64_t minimum,
                           std::uint64_t maximum, std::string_view name) {
  std::uint64_t result{};
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (error != std::errc{} || end != value.data() + value.size() ||
      result < minimum || result > maximum)
    throw std::invalid_argument(std::string(name) + " is out of range");
  return result;
}

template <typename Integer>
void write_little_endian(std::ostream &output, Integer value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    output.put(static_cast<char>(value & 0xff));
    value >>= 8;
  }
}
} // namespace

struct PcapWriter::State {
  explicit State(const std::filesystem::path &path)
      : output(path, std::ios::binary | std::ios::trunc) {}
  std::ofstream output;
};

void Metrics::observe_frame(std::size_t wire_size,
                            std::size_t copied_size) noexcept {
  ++frames;
  wire_bytes += wire_size;
  if (copied_size < wire_size)
    ++receive_truncations;
}

void Metrics::observe_kernel(std::uint64_t packets,
                             std::uint64_t drops) noexcept {
  kernel_packets += packets;
  kernel_drops += drops;
}

PcapWriter::PcapWriter(const std::filesystem::path &path,
                       std::uint64_t byte_limit)
    : state_(new State(path)), byte_limit_(byte_limit) {
  if (byte_limit < pcap_header_bytes + pcap_record_header_bytes) {
    delete state_;
    state_ = nullptr;
    throw std::invalid_argument("PCAP byte limit is too small");
  }
  if (!state_->output) {
    delete state_;
    state_ = nullptr;
    throw std::runtime_error("cannot open recorder PCAP file");
  }
  write_little_endian(state_->output, std::uint32_t{0xa1b2c3d4});
  write_little_endian(state_->output, std::uint16_t{2});
  write_little_endian(state_->output, std::uint16_t{4});
  write_little_endian(state_->output, std::int32_t{0});
  write_little_endian(state_->output, std::uint32_t{0});
  write_little_endian(state_->output,
                      static_cast<std::uint32_t>(receive_buffer_bytes));
  write_little_endian(state_->output, std::uint32_t{1});
  bytes_ = pcap_header_bytes;
}

PcapWriter::~PcapWriter() { delete state_; }

bool PcapWriter::write(std::span<const std::byte> frame, std::size_t wire_size,
                       std::chrono::system_clock::time_point timestamp) {
  if (limit_reached_)
    return false;
  const auto record_bytes = pcap_record_header_bytes + frame.size();
  if (record_bytes > byte_limit_ - bytes_) {
    limit_reached_ = true;
    return false;
  }
  const auto since_epoch = timestamp.time_since_epoch();
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
  const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
      since_epoch - seconds);
  write_little_endian(state_->output,
                      static_cast<std::uint32_t>(seconds.count()));
  write_little_endian(state_->output,
                      static_cast<std::uint32_t>(microseconds.count()));
  write_little_endian(state_->output,
                      static_cast<std::uint32_t>(frame.size()));
  write_little_endian(
      state_->output,
      static_cast<std::uint32_t>(std::min<std::size_t>(
          wire_size, std::numeric_limits<std::uint32_t>::max())));
  state_->output.write(reinterpret_cast<const char *>(frame.data()),
                       static_cast<std::streamsize>(frame.size()));
  if (!state_->output)
    throw std::runtime_error("cannot write recorder PCAP file");
  bytes_ += record_bytes;
  return true;
}

void PcapWriter::finish() {
  state_->output.flush();
  if (!state_->output)
    throw std::runtime_error("cannot finalize recorder PCAP file");
}

Options parse_options(int argc, char **argv) {
  Options options;
  bool duration_seen = false;
  bool capture_bytes_seen = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (index + 1 >= argc)
      throw std::invalid_argument(std::string(usage));
    const std::string_view value(argv[++index]);
    if (argument == "--interface" && options.interface.empty())
      options.interface = value;
    else if (argument == "--metrics" && options.metrics_path.empty())
      options.metrics_path = value;
    else if (argument == "--pcap" && !options.pcap_path)
      options.pcap_path = value;
    else if (argument == "--duration-seconds" && !duration_seen) {
      options.duration = std::optional<std::chrono::seconds>(
          std::chrono::seconds(parse_number(value, 1, maximum_duration.count(), "duration")));
      duration_seen = true;
    } else if (argument == "--capture-bytes" && !capture_bytes_seen) {
      options.capture_bytes = parse_number(
          value, pcap_header_bytes + pcap_record_header_bytes,
          maximum_capture_bytes, "capture byte limit");
      capture_bytes_seen = true;
    } else {
      throw std::invalid_argument(std::string(usage));
    }
  }
  if (options.interface.empty() || options.interface.size() > 15 ||
      options.metrics_path.empty() ||
      (options.pcap_path && options.pcap_path->empty()) ||
      (capture_bytes_seen && !options.pcap_path) ||
      (duration_seen && !options.pcap_path))
    throw std::invalid_argument(std::string(usage));
  if (options.pcap_path && !options.duration)
    options.duration = maximum_duration;
  return options;
}

nlohmann::json metrics_json(const Options &options, const Metrics &metrics,
                            std::chrono::milliseconds uptime,
                            std::int64_t observed_unix_ns,
                            const std::string &status) {
  return {{"schema", 1},
          {"role", "recorder"},
          {"status", status},
          {"passive", true},
          {"interface", options.interface},
          {"observation_clock", "unix"},
          {"observed_unix_ns", observed_unix_ns},
          {"uptime_ms", uptime.count()},
          {"limits",
           {{"duration_seconds", options.duration
                                     ? nlohmann::json(options.duration->count())
                                     : nlohmann::json(nullptr)},
            {"receive_buffer_bytes", receive_buffer_bytes},
            {"socket_receive_bytes", socket_receive_bytes},
            {"capture_bytes", options.pcap_path
                                  ? nlohmann::json(options.capture_bytes)
                                  : nlohmann::json(nullptr)}}},
          {"receive",
           {{"frames", metrics.frames},
            {"wire_bytes", metrics.wire_bytes},
            {"truncations", metrics.receive_truncations},
            {"errors", metrics.receive_errors},
            {"kernel_packets", metrics.kernel_packets},
            {"kernel_drops", metrics.kernel_drops}}},
          {"pcap",
           {{"enabled", options.pcap_path.has_value()},
            {"frames", metrics.pcap_frames},
            {"bytes", metrics.pcap_bytes},
            {"limit_reached", metrics.pcap_limit_reached},
            {"io_errors", metrics.pcap_io_errors}}}};
}

void write_metrics(const Options &options, const Metrics &metrics,
                   std::chrono::steady_clock::time_point started,
                   const std::string &status) {
  const auto parent = options.metrics_path.parent_path();
  if (!parent.empty())
    std::filesystem::create_directories(parent);
  const auto temporary = options.metrics_path.string() + ".tmp";
  std::ofstream output(temporary, std::ios::trunc);
  if (!output)
    throw std::runtime_error("cannot open recorder metrics file");
  const auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);
  const auto observed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
  output << metrics_json(options, metrics, uptime, observed, status).dump()
         << '\n';
  output.close();
  if (!output)
    throw std::runtime_error("cannot write recorder metrics file");
  std::filesystem::rename(temporary, options.metrics_path);
}
} // namespace sdr::recorder