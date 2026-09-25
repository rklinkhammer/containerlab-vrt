#include <sdr/telemetry.hpp>
#include "udp_pipeline.hpp"

#include <vita/codec/packet.hpp>
#include <vita/codec/samples.hpp>
#include <vita/profiles/iq/profile.hpp>
#include <vita/profiles/iq/sdr.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <complex>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace sdr::udp {
namespace {
using Json = nlohmann::json;
constexpr std::size_t maximum_udp_datagram = 65'535;
constexpr std::uint64_t picoseconds_per_second = 1'000'000'000'000ULL;

struct Socket {
  int descriptor{-1};
  Socket() = default;
  explicit Socket(int value) : descriptor(value) {}
  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;
  Socket(Socket &&other) noexcept
      : descriptor(std::exchange(other.descriptor, -1)) {}
  Socket &operator=(Socket &&other) noexcept {
    if (this != &other) {
      if (descriptor >= 0)
        close(descriptor);
      descriptor = std::exchange(other.descriptor, -1);
    }
    return *this;
  }
  ~Socket() {
    if (descriptor >= 0)
      close(descriptor);
  }
};

Json read_json(const std::string &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot open config: " + path);
  Json value;
  input >> value;
  return value;
}

std::uint16_t port(const Json &value) {
  const auto number = value.get<unsigned>();
  if (number == 0 || number > std::numeric_limits<std::uint16_t>::max())
    throw std::invalid_argument("UDP port is out of range");
  return static_cast<std::uint16_t>(number);
}

Endpoint endpoint(const Json &value, std::string_view host_key) {
  return {value.at(host_key).get<std::string>(), port(value.at("port"))};
}

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::invalid_argument(std::string(message));
}

addrinfo lookup(const Endpoint &endpoint_value, bool passive,
                std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> &results) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = passive ? AI_PASSIVE : 0;
  addrinfo *raw = nullptr;
  const auto service = std::to_string(endpoint_value.port);
  const int result = getaddrinfo(endpoint_value.host.c_str(), service.c_str(),
                                 &hints, &raw);
  if (result != 0)
    throw std::runtime_error(std::string("address lookup failed: ") +
                             gai_strerror(result));
  results.reset(raw);
  return *raw;
}

Socket bind_udp(const Endpoint &endpoint_value) {
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> results(nullptr,
                                                             freeaddrinfo);
  const auto address = lookup(endpoint_value, true, results);
  Socket socket_value(socket(address.ai_family, address.ai_socktype,
                             address.ai_protocol));
  if (socket_value.descriptor < 0)
    throw std::runtime_error("cannot create UDP socket");
  const int reuse = 1;
  if (setsockopt(socket_value.descriptor, SOL_SOCKET, SO_REUSEADDR, &reuse,
                 sizeof(reuse)) < 0 ||
      bind(socket_value.descriptor, address.ai_addr, address.ai_addrlen) < 0 ||
      fcntl(socket_value.descriptor, F_SETFL, O_NONBLOCK) < 0)
    throw std::runtime_error(std::string("cannot bind UDP socket: ") +
                             std::strerror(errno));
  return socket_value;
}

Socket connect_udp(const std::string &source_host,
                   const Endpoint &endpoint_value) {
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> results(nullptr,
                                                             freeaddrinfo);
  const auto address = lookup(endpoint_value, false, results);
  Socket socket_value(socket(address.ai_family, address.ai_socktype,
                             address.ai_protocol));
  sockaddr_in source{};
  source.sin_family = AF_INET;
  if (socket_value.descriptor < 0 ||
      inet_pton(AF_INET, source_host.c_str(), &source.sin_addr) != 1)
    throw std::invalid_argument("invalid UDP source address");
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(30);
  while (bind(socket_value.descriptor,
              reinterpret_cast<const sockaddr *>(&source), sizeof(source)) < 0) {
    if (errno != EADDRNOTAVAIL || std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error(std::string("cannot bind UDP source: ") +
                               std::strerror(errno));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (
      connect(socket_value.descriptor, address.ai_addr, address.ai_addrlen) < 0)
    throw std::runtime_error(std::string("cannot connect UDP socket: ") +
                             std::strerror(errno));
  return socket_value;
}

ProtocolTime add_samples(ProtocolTime time, std::uint64_t samples,
                         std::uint32_t rate) {
  const auto whole = samples / rate;
  const auto remainder = samples % rate;
  time.seconds += whole;
  const auto delta = remainder * picoseconds_per_second / rate;
  time.picoseconds += delta;
  time.seconds += time.picoseconds / picoseconds_per_second;
  time.picoseconds %= picoseconds_per_second;
  return time;
}

ProtocolTime subtract_samples(ProtocolTime time, std::uint64_t samples,
                              std::uint32_t rate) {
  auto whole = samples / rate;
  const auto remainder = samples % rate;
  const auto delta = remainder * picoseconds_per_second / rate;
  if (time.picoseconds < delta) {
    require(time.seconds > whole, "sample timestamp underflow");
    time.picoseconds += picoseconds_per_second;
    ++whole;
  }
  require(time.seconds >= whole, "sample timestamp underflow");
  time.seconds -= whole;
  time.picoseconds -= delta;
  return time;
}

bool valid_sdr_envelope(const vita::codec::Envelope &envelope,
                        std::uint32_t stream_id) {
  return envelope.type == vita::codec::PacketType::signal &&
         envelope.stream_id == stream_id &&
         envelope.class_id == vita::codec::ClassId{vita::profiles::iq::sdr_unknown_oui,
                                                    0, 0} &&
         envelope.timestamp.tsi == vita::codec::Tsi::utc &&
         envelope.timestamp.tsf == vita::codec::Tsf::picoseconds &&
         envelope.timestamp.fractional < picoseconds_per_second &&
         envelope.trailer;
}

} // namespace

struct ProcessorCore::StreamState {
  std::uint64_t center_hz{};
  std::uint32_t sample_rate_hz{};
  ProtocolTime epoch{};
  ProtocolTime window_begin{};
  std::vector<std::complex<double>> samples;
  std::vector<std::uint8_t> missing;
  std::optional<std::uint8_t> packet_count;
  std::size_t previous_pairs{};
  std::uint64_t ordinal{};
  std::uint64_t window_ordinal{};
  std::uint64_t discontinuities{};
  std::uint64_t sequence_gaps{};
  std::uint32_t sequence{};
  bool metadata_valid{};
};

ProcessorConfig load_processor_config(const std::string &path) {
  const auto root = read_json(path);
  require(root.at("schema") == 1 && root.at("role") == "processor",
          "invalid processor config identity");
  const auto &spectrum = root.at("spectrum");
  require(spectrum.at("schema") == 1 && spectrum.at("fft_size") == 2048 &&
              spectrum.at("overlap_samples") == 0 &&
              spectrum.at("window") == "rectangular",
          "unsupported processor spectrum config");
  const auto &radios = root.at("radios");
  require(radios.is_array() && radios.size() == 4,
          "processor requires four radios");
  ProcessorConfig result;
  const auto application_address =
      root.at("application").at("address").get<std::string>();
  const auto cidr = application_address.find('/');
  require(cidr != std::string::npos && cidr > 0,
          "invalid processor application address");
  result.source_host = application_address.substr(0, cidr);
  result.processing.fft_size = spectrum.at("fft_size").get<std::uint32_t>();
  result.processing.overlap_samples =
      spectrum.at("overlap_samples").get<std::uint32_t>();
  result.destination = endpoint(spectrum.at("destination"), "host");
  result.receive_bytes = std::min<std::size_t>(
      root.at("application").at("mtu").get<std::size_t>(),
      maximum_udp_datagram);
  result.queue_limit = root.at("limits").at("data_queue").get<std::size_t>();
  require(result.receive_bytes >= 32 && result.queue_limit > 0,
          "invalid processor bounds");
    ProcessorControllerConfig controller;
    controller.queue_limit =
      root.at("limits").at("control_queue").get<std::size_t>();
    controller.retry_limit =
      root.at("limits").at("retry_count").get<std::size_t>();
    require(controller.queue_limit > 0 && controller.queue_limit <= 64 &&
          controller.retry_limit > 0,
        "invalid processor control bounds");
  std::array<bool, 4> found{};
  for (const auto &radio : radios) {
    const auto sid = radio.at("sid").get<std::uint32_t>();
    require(sid >= 1 && sid <= 4 && !found[sid - 1],
            "invalid processor stream ID");
    found[sid - 1] = true;
    result.listeners[sid - 1] = {sid, endpoint(radio.at("iq"), "bind")};
    auto &target = controller.radios[sid - 1];
    target.id = radio.at("id").get<std::string>();
    target.sid = sid;
    const auto control = endpoint(radio.at("control"), "host");
    target.control_host = control.host;
    target.control_port = control.port;
    const auto status = endpoint(radio.at("status"), "host");
    target.status_host = status.host;
    target.status_port = status.port;
    const auto &settings = radio.at("settings");
    target.center_hz = settings.at("center_hz").get<std::uint64_t>();
    target.sample_rate_hz =
        settings.at("sample_rate_hz").get<std::uint32_t>();
    target.bandwidth_hz =
        settings.at("bandwidth_hz").get<std::uint32_t>();
    target.gain_db_q7 = settings.at("gain_db_q7").get<std::int16_t>();
    require(!target.id.empty() && target.center_hz > 0 &&
                target.sample_rate_hz > 0 && target.bandwidth_hz > 0 &&
                target.bandwidth_hz <= target.sample_rate_hz,
            "invalid processor radio settings");
  }
  result.controller = std::move(controller);
  return result;
}

DetectorConfig load_detector_config(const std::string &path) {
  const auto root = read_json(path);
  require(root.at("schema") == 1 && root.at("role") == "detector",
          "invalid detector config identity");
  const auto &spectrum = root.at("spectrum");
  require(spectrum.at("schema") == 1 && spectrum.at("fft_size") == 2048 &&
              spectrum.at("overlap_samples") == 0 &&
              spectrum.at("window") == "rectangular",
          "unsupported detector spectrum config");
  DetectorConfig result;
  result.listener = endpoint(spectrum, "bind");
  result.fft_size = spectrum.at("fft_size").get<std::uint32_t>();
  result.receive_bytes = spectrum_wire_size(result.fft_size);
  return result;
}

ProcessorCore::ProcessorCore(ProcessorConfig config)
    : config_(std::move(config)), streams_(new std::array<StreamState, 4>) {
  config_.processing.validate();
  for (auto &stream : *streams_) {
    stream.samples.reserve(config_.processing.fft_size);
    stream.missing.reserve(config_.processing.fft_size);
  }
}

ProcessorCore::~ProcessorCore() { delete streams_; }

std::vector<std::vector<std::byte>>
ProcessorCore::consume(std::uint32_t listener_stream_id,
                       std::span<const std::byte> datagram) {
  ++metrics_.datagrams;
  std::vector<std::vector<std::byte>> output;
  if (listener_stream_id < 1 || listener_stream_id > 4 ||
      datagram.size() > config_.receive_bytes) {
    ++metrics_.malformed;
    return output;
  }
  auto &stream = (*streams_)[listener_stream_id - 1];
  const vita::Bytes bytes{datagram.data(), datagram.size()};
  const auto envelope = vita::codec::decode_envelope(bytes);
  if (!envelope || envelope->envelope.stream_id != listener_stream_id) {
    ++metrics_.malformed;
    return output;
  }
  if (envelope->envelope.type == vita::codec::PacketType::context) {
    const auto packet = vita::codec::decode_packet(bytes);
    if (!packet || packet->envelope.envelope.class_id ||
        packet->envelope.envelope.timestamp.tsi != vita::codec::Tsi::utc ||
        packet->envelope.envelope.timestamp.tsf !=
            vita::codec::Tsf::picoseconds ||
        packet->envelope.envelope.timestamp.fractional >=
            picoseconds_per_second) {
      ++metrics_.malformed;
      return output;
    }
    if (packet->fields.size() != 4) {
      ++metrics_.malformed;
      return output;
    }
    std::optional<std::uint64_t> center;
    std::optional<std::uint32_t> rate;
    std::optional<std::int64_t> bandwidth;
    std::optional<vita::GainStages> gain;
    for (std::size_t index = 0; index < packet->fields.size(); ++index) {
      const auto &field = packet->fields[index];
      if (field.kind != vita::BodyKind::values ||
          field.attribute != vita::Attribute::current) {
        ++metrics_.malformed;
        return output;
      }
      const auto value = field.value();
      if (!value) {
        ++metrics_.malformed;
        return output;
      }
      if (field.id == vita::RFReferenceFrequency::id) {
        const auto *hertz = std::get_if<vita::Hertz>(&*value);
        if (!hertz || hertz->q20 < 0 || hertz->q20 % (1LL << 20)) {
          ++metrics_.malformed;
          return output;
        }
        center = static_cast<std::uint64_t>(hertz->q20 >> 20);
      } else if (field.id == vita::SampleRate::id) {
        const auto *hertz = std::get_if<vita::Hertz>(&*value);
        if (!hertz || hertz->q20 <= 0 || hertz->q20 % (1LL << 20) ||
            (hertz->q20 >> 20) > std::numeric_limits<std::uint32_t>::max()) {
          ++metrics_.malformed;
          return output;
        }
        rate = static_cast<std::uint32_t>(hertz->q20 >> 20);
      } else if (field.id == vita::Bandwidth::id) {
        const auto *hertz = std::get_if<vita::Hertz>(&*value);
        if (!hertz) {
          ++metrics_.malformed;
          return output;
        }
        bandwidth = hertz->q20;
      } else if (field.id == vita::Gain::id) {
        const auto *stages = std::get_if<vita::GainStages>(&*value);
        if (!stages) {
          ++metrics_.malformed;
          return output;
        }
        gain = *stages;
      } else {
        ++metrics_.malformed;
        return output;
      }
    }
    if (!center || !rate || !bandwidth || !gain || *bandwidth <= 0 ||
      *bandwidth > static_cast<std::int64_t>(*rate) * (1LL << 20) ||
      gain->stage2_q7 != 0 || gain->stage1_q7 < -60 * 128 ||
      gain->stage1_q7 > 60 * 128) {
      ++metrics_.malformed;
      return output;
    }
    try {
      ProcessingConfig{.sample_rate_hz = *rate,
                       .fft_size = config_.processing.fft_size,
                       .overlap_samples = 0}
          .validate();
      require(*center >= vita::profiles::iq::minimum_center_hz &&
                  *center <= vita::profiles::iq::maximum_center_hz,
              "invalid center frequency");
    } catch (const std::invalid_argument &) {
      ++metrics_.malformed;
      return output;
    }
    if (!stream.samples.empty() &&
        (stream.center_hz != *center || stream.sample_rate_hz != *rate)) {
      stream.samples.clear();
      stream.missing.clear();
      ++stream.discontinuities;
    }
    stream.center_hz = *center;
    stream.sample_rate_hz = *rate;
    stream.epoch = {packet->envelope.envelope.timestamp.integer,
                    packet->envelope.envelope.timestamp.fractional};
    stream.metadata_valid = true;
    ++metrics_.context_updates;
    return output;
  }
  if (!stream.metadata_valid ||
      !valid_sdr_envelope(envelope->envelope, listener_stream_id) ||
      !envelope->trailer ||
      (*envelope->trailer & ~std::uint32_t{0x00000c00}) != 0x00c00000) {
    ++metrics_.malformed;
    return output;
  }
  const auto sample_view =
      vita::codec::SampleView<std::int16_t>::create(envelope->payload);
  if (!sample_view || sample_view->size() == 0 ||
      sample_view->size() > config_.processing.fft_size) {
    ++metrics_.malformed;
    return output;
  }
  std::size_t missing_pairs = 0;
  if (stream.packet_count) {
    const auto expected = static_cast<std::uint8_t>((*stream.packet_count + 1) & 15);
    const auto gap = static_cast<std::uint8_t>((envelope->envelope.packet_count -
                                               expected) & 15);
    if (gap) {
      ++stream.discontinuities;
      stream.sequence_gaps += gap;
      metrics_.packet_gaps += gap;
      missing_pairs = gap * stream.previous_pairs;
    }
  }
  stream.packet_count = envelope->envelope.packet_count;
  stream.previous_pairs = sample_view->size();
  const ProtocolTime packet_time{envelope->envelope.timestamp.integer,
                                 envelope->envelope.timestamp.fractional};
  ProtocolTime missing_begin = packet_time;
  try {
    if (missing_pairs)
      missing_begin =
          subtract_samples(packet_time, missing_pairs, stream.sample_rate_hz);
  } catch (const std::invalid_argument &) {
    ++metrics_.malformed;
    return output;
  }
  auto append = [&](std::complex<double> sample, bool missing,
                    ProtocolTime begin) {
    if (stream.samples.empty()) {
      stream.window_begin = begin;
      stream.window_ordinal = stream.ordinal;
    }
    stream.samples.push_back(sample);
    stream.missing.push_back(missing ? 1 : 0);
    ++stream.ordinal;
    if (stream.samples.size() == config_.processing.fft_size) {
      auto spectrum = power_spectrum(
          {.sample_rate_hz = stream.sample_rate_hz,
           .fft_size = config_.processing.fft_size,
           .overlap_samples = 0},
          stream.samples, stream.missing);
      spectrum.stream_id = listener_stream_id;
      spectrum.sequence = stream.sequence++;
      spectrum.center_hz = stream.center_hz;
      spectrum.begin = stream.window_begin;
      spectrum.end = add_samples(stream.window_begin,
                                 config_.processing.fft_size,
                                 stream.sample_rate_hz);
      spectrum.epoch = stream.epoch;
      spectrum.ordinal = stream.window_ordinal;
      spectrum.discontinuities = stream.discontinuities;
      spectrum.sequence_gaps = stream.sequence_gaps;
      if (output.size() < config_.queue_limit) {
        output.push_back(encode_spectrum(spectrum));
        ++metrics_.spectra;
      } else {
        ++metrics_.unavailable;
      }
      stream.samples.clear();
      stream.missing.clear();
    }
  };
  for (std::size_t index = 0; index < missing_pairs; ++index)
    append({}, true,
           add_samples(missing_begin, index, stream.sample_rate_hz));
  for (std::size_t index = 0; index < sample_view->size(); ++index) {
    const auto pair = sample_view->at(index);
    if (!pair) {
      ++metrics_.malformed;
      return {};
    }
    append({static_cast<double>(pair->i) / 32768.0,
            static_cast<double>(pair->q) / 32768.0},
           false, packet_time);
  }
  return output;
}

std::optional<Detection>
DetectorCore::consume(std::span<const std::byte> datagram) {
  ++metrics_.datagrams;
  if (datagram.size() > config_.receive_bytes) {
    ++metrics_.malformed;
    return std::nullopt;
  }
  try {
    const auto spectrum = decode_spectrum(datagram);
    if (spectrum.fft_size != config_.fft_size) {
      ++metrics_.malformed;
      return std::nullopt;
    }
    if (spectrum.stream_id < 1 || spectrum.stream_id > sequences_.size()) {
      ++metrics_.malformed;
      return std::nullopt;
    }
    auto &previous_sequence = sequences_[spectrum.stream_id - 1];
    if (previous_sequence) {
      const auto expected = *previous_sequence + 1;
      if (spectrum.sequence != expected)
        ++metrics_.sequence_gaps;
    }
    previous_sequence = spectrum.sequence;
    seen_[spectrum.stream_id - 1] = true;
    Detection result{.stream_id = spectrum.stream_id,
                     .observation_time = spectrum.begin,
                     .frequency_hz = detect_frequency(spectrum),
                     .validity = "valid"};
    if (std::any_of(spectrum.gaps.begin(), spectrum.gaps.end(),
                    [](std::uint8_t value) { return value != 0; })) {
      result.validity = "gapped";
      ++metrics_.gapped;
    } else if (!result.frequency_hz) {
      result.validity = "zero";
      ++metrics_.zero;
    } else {
      ++metrics_.detections;
    }
    return result;
  } catch (const std::invalid_argument &) {
    ++metrics_.malformed;
    return std::nullopt;
  }
}

void DetectorCore::finish() {
  metrics_.unavailable += std::count(seen_.begin(), seen_.end(), false);
}

std::string detection_json(const Detection &detection) {
  return Json{{"type", "detection"},
              {"stream_id", detection.stream_id},
              {"observation_time",
               {{"seconds", detection.observation_time.seconds},
                {"picoseconds", detection.observation_time.picoseconds}}},
              {"detected_frequency_hz",
               detection.frequency_hz ? Json(*detection.frequency_hz) : Json(nullptr)},
              {"validity", detection.validity}}
      .dump();
}

std::string metrics_json(const ProcessorMetrics &metrics) {
  return Json{{"type", "metrics"},
              {"observed_unix_ns",
               std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count()},
              {"datagrams", metrics.datagrams},
              {"malformed", metrics.malformed},
              {"context_updates", metrics.context_updates},
              {"packet_gaps", metrics.packet_gaps},
              {"spectra", metrics.spectra},
              {"send_failures", metrics.send_failures},
              {"unavailable", metrics.unavailable}}
      .dump();
}

std::string metrics_json(const DetectorMetrics &metrics) {
  return Json{{"type", "metrics"},
              {"observed_unix_ns",
               std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count()},
              {"datagrams", metrics.datagrams},
              {"detections", metrics.detections},
              {"malformed", metrics.malformed},
              {"sequence_gaps", metrics.sequence_gaps},
              {"gapped", metrics.gapped},
              {"zero", metrics.zero},
              {"unavailable", metrics.unavailable}}
      .dump();
}

int run_processor(const ProcessorConfig &config,
                  std::optional<std::chrono::milliseconds> duration,
                  std::ostream &output,
                  bool (*stop_requested)() noexcept) {
  ProcessorCore core(config);
  std::array<Socket, 4> listeners;
  std::array<pollfd, 4> polls{};
  for (std::size_t index = 0; index < listeners.size(); ++index) {
    listeners[index] = bind_udp(config.listeners[index].endpoint);
    polls[index] = {listeners[index].descriptor, POLLIN, 0};
  }
  auto destination = connect_udp(config.source_host, config.destination);
  std::vector<std::byte> receive_buffer(config.receive_bytes);
  const auto started = std::chrono::steady_clock::now();
  auto next_metrics = started + std::chrono::seconds(1);
  sdr::telemetry::Reporter telemetry("processor",output);
  std::uint64_t rx_bytes=0, tx_bytes=0, tx_packets=0, processing_ns=0, processed=0;
  auto report=[&](bool final=false){
    if(!final&&!telemetry.due())return;
    auto counters=Json::parse(metrics_json(core.metrics()));
    counters.erase("type");
    counters["rx_bytes"]=rx_bytes;
    counters["tx_bytes"]=tx_bytes;
    counters["tx_packets"]=tx_packets;
    counters["processing_ns_total"]=processing_ns;
    counters["processing_calls"]=processed;
    counters["queue_depth"]=nullptr;
    counters["proven_packet_loss"]=nullptr;
    counters["downstream_delivery"]=nullptr;
    telemetry.heartbeat(counters,core.metrics().malformed + core.metrics().send_failures,final);
  };

    while ((!stop_requested || !stop_requested()) &&
      (!duration || std::chrono::steady_clock::now() - started < *duration)) {
    report();
    const auto ready = poll(polls.data(), polls.size(), duration ? 10 : 100);
    if (ready < 0 && errno != EINTR)
      throw std::runtime_error("processor poll failed");
    for (std::size_t index = 0; index < polls.size(); ++index) {
      if (!(polls[index].revents & POLLIN))
        continue;
      const auto received = recv(listeners[index].descriptor,
                                 receive_buffer.data(), receive_buffer.size(), 0);
      if (received <= 0)
        continue;
      rx_bytes+=static_cast<std::uint64_t>(received);
      const auto before=core.metrics().malformed;
      const auto processing_start=std::chrono::steady_clock::now();
      auto spectra = core.consume(config.listeners[index].stream_id,
                                  std::span{receive_buffer}.first(received));
      processing_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-processing_start).count();
      ++processed;
      if(core.metrics().malformed==before)telemetry.activity();
      for (const auto &wire : spectra)
        if (send(destination.descriptor, wire.data(), wire.size(), 0) !=
            static_cast<ssize_t>(wire.size()))
          core.note_send_failure();
        else {++tx_packets;tx_bytes+=wire.size();}
    }
    if (std::chrono::steady_clock::now() >= next_metrics) {
      sdr::telemetry::write_line(output,metrics_json(core.metrics()));
      next_metrics += std::chrono::seconds(1);
    }
  }
  report(true);
  sdr::telemetry::write_line(output,metrics_json(core.metrics()));
  return 0;
}

int run_detector(const DetectorConfig &config,
                 std::optional<std::chrono::milliseconds> duration,
                 std::ostream &output,
                 bool (*stop_requested)() noexcept) {
  auto listener = bind_udp(config.listener);
  DetectorCore core(config);
  std::vector<std::byte> receive_buffer(config.receive_bytes);
  pollfd descriptor{listener.descriptor, POLLIN, 0};
  const auto started = std::chrono::steady_clock::now();
  auto next_metrics = started + std::chrono::seconds(1);
  sdr::telemetry::Reporter telemetry("detector",output);
  std::uint64_t rx_bytes=0, processing_ns=0, processed=0;
  auto report=[&](bool final=false){
    if(!final&&!telemetry.due())return;
    auto counters=Json::parse(metrics_json(core.metrics()));
    counters.erase("type");
    counters["rx_bytes"]=rx_bytes;
    counters["tx_bytes"]=nullptr;
    counters["tx_packets"]=nullptr;
    counters["processing_ns_total"]=processing_ns;
    counters["processing_calls"]=processed;
    counters["queue_depth"]=nullptr;
    counters["proven_packet_loss"]=nullptr;
    counters["downstream_delivery"]=nullptr;
    telemetry.heartbeat(counters,core.metrics().malformed,final);
  };

  std::array<std::optional<std::uint64_t>, 4> reported_seconds{};
  std::array<std::optional<std::chrono::steady_clock::time_point>, 4> last_seen{};
  std::array<bool, 4> unavailable_reported{};
    while ((!stop_requested || !stop_requested()) &&
      (!duration || std::chrono::steady_clock::now() - started < *duration)) {
    report();
    const auto ready = poll(&descriptor, 1, duration ? 10 : 100);
    if (ready < 0 && errno != EINTR)
      throw std::runtime_error("detector poll failed");
    const auto received = (descriptor.revents & POLLIN) ? recv(listener.descriptor, receive_buffer.data(), receive_buffer.size(), 0) : 0;
    std::optional<Detection> detection;
    if(received>0){
      rx_bytes+=static_cast<std::uint64_t>(received);
      const auto before=core.metrics().malformed;
      const auto processing_start=std::chrono::steady_clock::now();
      detection=core.consume(std::span{receive_buffer}.first(received));
      processing_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-processing_start).count();
      ++processed;
      if(core.metrics().malformed==before)telemetry.activity();
    }
    if (detection && detection->stream_id >= 1 && detection->stream_id <= 4) {
      const auto stream_index = detection->stream_id - 1;
      last_seen[stream_index] = std::chrono::steady_clock::now();
      unavailable_reported[stream_index] = false;
      telemetry.detection(Json::parse(detection_json(*detection)));
      auto &reported = reported_seconds[detection->stream_id - 1];
      if (!reported || *reported != detection->observation_time.seconds) {
        sdr::telemetry::write_line(output,detection_json(*detection));
        reported = detection->observation_time.seconds;
      }
    }
    const auto now = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < last_seen.size(); ++index) {
      if (!last_seen[index] || unavailable_reported[index] ||
          now - *last_seen[index] < std::chrono::seconds(5))
        continue;
      const auto wall = std::chrono::system_clock::now().time_since_epoch();
      const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(wall);
      Detection unavailable{
          .stream_id = static_cast<std::uint32_t>(index + 1),
          .observation_time = {
              static_cast<std::uint64_t>(seconds.count()),
              static_cast<std::uint64_t>(
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                      wall - seconds)
                      .count()) *
                  1000},
          .frequency_hz = std::nullopt,
          .validity = "unavailable"};
      telemetry.detection(Json::parse(detection_json(unavailable)));
      sdr::telemetry::write_line(output,detection_json(unavailable));
      core.note_unavailable();
      unavailable_reported[index] = true;
    }
    if (now >= next_metrics) {
      sdr::telemetry::write_line(output,metrics_json(core.metrics()));
      next_metrics += std::chrono::seconds(1);
    }
  }
  core.finish();
  report(true);
  sdr::telemetry::write_line(output,metrics_json(core.metrics()));
  return 0;
}

std::optional<std::chrono::milliseconds>
parse_duration(int argc, char **argv, std::string &config_path) {
  std::optional<std::chrono::milliseconds> duration;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--config" && index + 1 < argc) {
      config_path = argv[++index];
    } else if (argument == "--duration-seconds" && index + 1 < argc) {
      const std::string value(argv[++index]);
      std::size_t consumed = 0;
      const double seconds = std::stod(value, &consumed);
      require(consumed == value.size() && std::isfinite(seconds) && seconds >= 0,
              "invalid duration");
            require(seconds <= static_cast<double>(
                 std::numeric_limits<std::chrono::milliseconds::rep>::max()) /
                 1000.0,
              "duration is out of range");
      duration = std::chrono::milliseconds(
          static_cast<std::chrono::milliseconds::rep>(seconds * 1000));
    } else {
      throw std::invalid_argument("usage: --config PATH [--duration-seconds N]");
    }
  }
  return duration;
}
} // namespace sdr::udp