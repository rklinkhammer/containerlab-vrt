#include <sdr/radio_services.hpp>

#include <poll.h>
#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace sdr {
namespace {
std::uint16_t port(const nlohmann::json &value) {
  const auto number = value.get<unsigned>();
  if (number == 0 || number > std::numeric_limits<std::uint16_t>::max())
    throw std::invalid_argument("port is out of range");
  return static_cast<std::uint16_t>(number);
}

bool wait_for(int descriptor, short events,
              std::chrono::steady_clock::time_point deadline) noexcept {
  const auto now = std::chrono::steady_clock::now();
  if (now >= deadline)
    return false;
  const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
  pollfd item{descriptor, events, 0};
  return poll(&item, 1, static_cast<int>(remaining.count())) == 1 &&
         (item.revents & events) != 0;
}
} // namespace

RadioConfig load_radio_config(const std::string &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot open radio config: " + path);
  nlohmann::json root;
  input >> root;
  if (root.at("schema") != 1 || root.at("role") != "radio")
    throw std::invalid_argument("invalid radio config identity");
  const auto &radio = root.at("radio");
  const auto &application = root.at("application");
  const auto &defaults = root.at("defaults");
  const auto &limits = root.at("limits");
  RadioConfig config;
  config.id = radio.at("id").get<std::string>();
  const auto application_address = radio.at("address").get<std::string>();
  const auto cidr = application_address.find('/');
  if (cidr == std::string::npos || cidr == 0)
    throw std::invalid_argument("invalid radio application address");
  config.application_host = root.at("control").at("bind").get<std::string>();
  config.data_source_host = application_address.substr(0, cidr);
  config.application_mtu = application.at("mtu").get<std::size_t>();
  config.sid = radio.at("sid").get<std::uint32_t>();
  config.signal_hz = radio.at("signal_hz").get<std::uint64_t>();
  config.amplitude = defaults.at("amplitude").get<double>();
  config.phase_radians = radio.at("phase_millidegrees").get<double>() *
                         std::numbers::pi / 180'000.0;
  config.defaults.center_hz = defaults.at("center_hz").get<std::uint64_t>();
  config.defaults.sample_rate_hz =
      defaults.at("sample_rate_hz").get<std::uint32_t>();
  config.defaults.bandwidth_hz =
      defaults.at("bandwidth_hz").get<std::uint32_t>();
  config.defaults.gain_db = defaults.at("gain_db_q7").get<double>() / 128.0;
  config.iq_host = root.at("iq_destination").at("host").get<std::string>();
  config.iq_port = port(root.at("iq_destination").at("port"));
  config.control_port = port(radio.at("control_port"));
  config.status_bind = root.at("status").at("bind").get<std::string>();
  config.status_port = port(root.at("status").at("port"));
  config.control_queue = limits.at("control_queue").get<std::size_t>();
  config.status_connections = limits.at("status_connections").get<std::size_t>();
  config.samples_per_packet = defaults.at("samples_per_packet").get<std::size_t>();
    config.burst_samples = defaults.at("burst_samples").get<std::size_t>();
    if (config.id.empty() || config.application_mtu < 576 || config.sid == 0 ||
      config.control_queue == 0 ||
      config.status_connections == 0 || config.status_connections > 4 ||
      config.samples_per_packet == 0 || config.samples_per_packet > 1024 ||
      config.burst_samples < config.samples_per_packet)
    throw std::invalid_argument("invalid radio config bounds");
  VirtualRadio validation(config.signal_hz, config.amplitude,
                          config.phase_radians);
  validation.configure(config.defaults);
  return config;
}

nlohmann::json radio_status(const RadioConfig &config,
                            const std::string &boot_id,
                            const ControlSnapshot &control,
                            const RadioSettings &settings,
                            std::uint64_t sample_ordinal,
                            std::uint64_t clipped_samples, bool streaming,
                            std::uint64_t uptime_ms,
                            std::uint64_t observation_unix_ns, bool ready) {
  nlohmann::json connection{{"active", control.occupied}};
  if (control.generation)
    connection["generation"] = *control.generation;
  else
    connection["generation"] = nullptr;
  return {{"version", 1},
          {"radio_id", config.id},
          {"boot_id", boot_id},
          {"uptime_ms", uptime_ms},
          {"ready", ready},
          {"observation", {{"timestamp_unix_ns", observation_unix_ns},
                            {"clock_domain", "utc"}}},
          {"connection", connection},
          {"streaming", streaming},
          {"sample_ordinal", sample_ordinal},
          {"clipped_samples", clipped_samples},
          {"settings", {{"center_hz", settings.center_hz},
                        {"sample_rate_hz", settings.sample_rate_hz},
                        {"bandwidth_hz", settings.bandwidth_hz},
                        {"gain_db", settings.gain_db}}}};
}

bool serve_status_transaction(int descriptor, const nlohmann::json &status,
                              std::chrono::milliseconds timeout) noexcept {
  try {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    StatusFrameDecoder decoder;
    std::array<std::byte, 256> buffer{};
    while (!decoder.complete()) {
      if (!wait_for(descriptor, POLLIN, deadline))
        return false;
      const auto count = recv(descriptor, buffer.data(), buffer.size(), 0);
      if (count <= 0 || !decoder.feed({buffer.data(), static_cast<std::size_t>(count)}))
        return false;
    }
    static_cast<void>(decoder.take_request());
    const auto response = encode_status_response(status);
    std::size_t offset = 0;
    while (offset < response.size()) {
      if (!wait_for(descriptor, POLLOUT, deadline))
        return false;
      const auto count = send(descriptor, response.data() + offset,
                              response.size() - offset, 0);
      if (count <= 0)
        return false;
      offset += static_cast<std::size_t>(count);
    }
    return true;
  } catch (...) {
    return false;
  }
}

VrtControlFramer::VrtControlFramer(std::size_t maximum_packet_bytes,
                                   std::size_t packet_limit,
                                   PacketHandler handler)
    : framer_(maximum_packet_bytes), packet_limit_(packet_limit),
      handler_(std::move(handler)) {
  if (packet_limit_ == 0 || !handler_)
    throw std::invalid_argument("invalid control framing bounds");
}

vita::Result<void> VrtControlFramer::deliver(void *context,
                                             vita::Bytes packet) noexcept {
  auto &self = *static_cast<VrtControlFramer *>(context);
  if (self.packets_ >= self.packet_limit_)
    return std::unexpected(vita::Error{vita::ErrorCode::capacity_exhausted});
  try {
    if (!self.handler_({packet.data(), packet.size()}))
      return std::unexpected(vita::Error{vita::ErrorCode::callback_failure});
  } catch (...) {
    return std::unexpected(vita::Error{vita::ErrorCode::callback_failure});
  }
  ++self.packets_;
  return {};
}

bool VrtControlFramer::feed(std::span<const std::byte> input) noexcept {
  return framer_.feed({input.data(), input.size()}, this, deliver).has_value();
}

bool VrtControlFramer::disconnect() noexcept {
  return framer_.disconnect().has_value();
}
} // namespace sdr