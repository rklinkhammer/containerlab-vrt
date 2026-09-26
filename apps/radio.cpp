#include <sdr/telemetry.hpp>
#include <sdr/radio_services.hpp>
#include <sdr/radio_transport.hpp>
#include <sdr/vrt_radio_adapter.hpp>

#include <vita/profiles/iq/lab.hpp>
#include <vita/runtime/public/runtime.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> running{true};
std::atomic<bool> restart_requested{false};

void stop(int) { running.store(false); }
void restart(int) {
  restart_requested.store(true);
  running.store(false);
}

struct Descriptor {
  int value{-1};
  explicit Descriptor(int descriptor = -1) : value(descriptor) {}
  Descriptor(const Descriptor &) = delete;
  Descriptor &operator=(const Descriptor &) = delete;
  Descriptor(Descriptor &&other) noexcept
      : value(std::exchange(other.value, -1)) {}
  ~Descriptor() {
    if (value >= 0)
      close(value);
  }
};

Descriptor tcp_listener(const std::string &host, std::uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  addrinfo *raw = nullptr;
  const auto service = std::to_string(port);
  if (getaddrinfo(host.c_str(), service.c_str(), &hints, &raw) != 0)
    throw std::runtime_error("status address lookup failed");
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(raw, freeaddrinfo);
  Descriptor listener(socket(raw->ai_family, raw->ai_socktype, raw->ai_protocol));
  const int reuse = 1;
  if (listener.value < 0 ||
      setsockopt(listener.value, SOL_SOCKET, SO_REUSEADDR, &reuse,
                 sizeof(reuse)) < 0 ||
      bind(listener.value, raw->ai_addr, raw->ai_addrlen) < 0 ||
      listen(listener.value, 4) < 0 ||
      fcntl(listener.value, F_SETFL, O_NONBLOCK) < 0)
    throw std::runtime_error("cannot bind status listener");
  return listener;
}

std::string boot_id() {
  std::random_device source;
  constexpr char digits[] = "0123456789abcdef";
  std::string result(32, '0');
  for (char &value : result)
    value = digits[source() & 0xf];
  return result;
}

struct StatusCache {
  std::mutex mutex;
  std::condition_variable updated;
  std::atomic<bool> requested{false};
  std::uint64_t sequence{};
  nlohmann::json value;
};

void status_worker(int listener, StatusCache &cache) {
  while (running.load()) {
    pollfd item{listener, POLLIN, 0};
    if (poll(&item, 1, 100) != 1) continue;
    Descriptor client(accept(listener, nullptr, nullptr));
    if (client.value < 0) continue;
    nlohmann::json status;
    {
      std::unique_lock lock(cache.mutex);
      const auto before=cache.sequence;
      cache.requested.store(true);
      if(!cache.updated.wait_for(lock,std::chrono::milliseconds(500),[&]{return cache.sequence>before;}))continue;
      status=cache.value;
    }
    static_cast<void>(sdr::serve_status_transaction(client.value,status));
  }
}

vita::runtime::timing::ProtocolTime utc_now() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now);
  const auto picoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               now - seconds)
                               .count() *
                           1000ULL;
  return {static_cast<std::uint64_t>(seconds.count()), picoseconds};
}
} // namespace

int main(int argc, char **argv) {
  try {
    std::string config_path;
    if (argc == 2)
      config_path = argv[1];
    else if (argc == 3 && std::string_view(argv[1]) == "--config")
      config_path = argv[2];
    else
      throw std::invalid_argument("usage: radio [--config] GENERATED_RADIO_JSON");
    const auto config = sdr::load_radio_config(config_path);
    sdr::telemetry::Reporter telemetry("radio",std::cout,config.id);
    auto device = std::make_shared<sdr::SoapyVirtualDevice>(
        config.id, config.signal_hz, config.amplitude, config.phase_radians);
    device->apply_settings(config.defaults);
    auto adapter = sdr::VrtRadioAdapter::create(device);
    sdr::ControlSlot control;
    sdr::RadioTransportFactory transport{
        {.control_host = config.application_host,
         .control_port = config.control_port,
         .data_source_host = config.data_source_host,
         .data_host = config.iq_host,
         .data_port = config.iq_port,
         .sid = config.sid,
         .control_queue = config.control_queue},
        &control,
        {}};
    auto runtime_config = vita::profiles::iq::lab::config(
        vita::profiles::iq::sdr_unknown_oui);
    vita::profiles::iq::lab::PoolCounts pool_counts;
    pool_counts.payload_bytes = config.samples_per_packet * 4;
    pool_counts.rx_data_bytes = 8192;
    auto pools = vita::profiles::iq::lab::pools(pool_counts);
    if (!runtime_config || !pools)
      throw std::runtime_error("cannot allocate radio runtime");
    runtime_config->clock.epoch = vita::runtime::timing::Epoch::utc;
    runtime_config->timing.device_late_ps = 100'000'000'000ULL;
    runtime_config->transport = sdr::radio_transport_factory(transport);
    using Runtime = vita::VitaRuntime<1, 16, 256, 1024 * 1024>;
    auto runtime = Runtime::create(*runtime_config, std::move(*pools));
    if (!runtime)
      throw std::runtime_error("radio runtime setup failed");
    vita::StreamConfig stream;
    stream.sid = config.sid;
    stream.controller_id = 1;
    stream.controllee_id = 2;
    stream.profile = vita::profiles::iq::Profile::sdr_radio;
    stream.role = vita::EndpointRole::controllee_only;
    stream.sample_rate = config.defaults.sample_rate_hz;
    stream.center_frequency = config.defaults.center_hz;
    stream.bandwidth = config.defaults.bandwidth_hz;
    stream.gain = {static_cast<std::int16_t>(config.defaults.gain_db * 128.0),
                   0};
    stream.ip_mtu = config.application_mtu;
    stream.maximum_samples_per_packet = config.samples_per_packet;
    stream.burst_pairs = config.burst_samples;
    stream.trailer = true;
    stream.source = adapter->source_provider();
    stream.device = adapter->device_binding();
    auto controllee = (*runtime)->add_controllee(stream);
    if (!controllee)
      throw std::runtime_error("radio controllee setup failed");
    const auto origin = std::chrono::steady_clock::now();
    const auto clock_origin = utc_now();
    if (!(*runtime)->observe_pps({0}, clock_origin))
      throw std::runtime_error("radio clock setup failed");

    auto listener = tcp_listener(config.status_bind, config.status_port);
    const auto id = boot_id();
    const auto started = std::chrono::steady_clock::now();
    StatusCache status_cache;
    std::vector<std::jthread> status_workers;
    for (std::size_t index = 0; index < config.status_connections; ++index)
      status_workers.emplace_back(status_worker,listener.value,std::ref(status_cache));
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    std::signal(SIGHUP, restart);
    std::uint64_t last_completed=0;
    auto report=[&](bool final=false){
      auto m=transport.instance->metrics();
      if(m.completed_submissions!=last_completed){telemetry.activity();last_completed=m.completed_submissions;}
      if(final||telemetry.due())telemetry.heartbeat({{"control_rx_packets",m.received_packets},{"control_rx_bytes",m.received_bytes},{"tx_bytes",m.transmitted_bytes},{"tx_completed_submissions",m.completed_submissions},{"tx_failed_submissions",m.failed_submissions},{"write_retries",m.write_retries},{"sample_ordinal",device->sample_ordinal()},{"clipped_samples",device->clipped_samples()},{"streaming",device->streaming()},{"queue_depth",nullptr},{"proven_packet_loss",nullptr},{"downstream_delivery",nullptr}},m.failed_submissions,final);
    };
    std::uint64_t last_clock_second = 0;
    while (running.load()) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now() - origin)
                               .count();
      const auto monotonic = static_cast<std::uint64_t>(elapsed);
      const auto second = monotonic / 1'000'000'000;
      if (second > last_clock_second) {
        auto pulse_time = clock_origin;
        pulse_time.seconds += second;
        if (!(*runtime)->observe_pps({second * 1'000'000'000}, pulse_time)) {
          running.store(false);
          throw std::runtime_error("radio clock observation failed");
        }
        last_clock_second = second;
      }
      auto progressed = (*runtime)->progress({monotonic});
      if (!progressed && !progressed.error().retryable) {
        running.store(false);
        throw std::runtime_error("radio runtime progress failed");
      }
      // Snapshot in the serialized runtime domain, after progress. Status threads
      // never read native state or assemble a watermark from a different session.
      if(status_cache.requested.exchange(false)) {
        auto snapshot=sdr::radio_status(config,id,control.snapshot(),device->settings(),
          device->sample_ordinal(),device->clipped_samples(),device->streaming(),
          static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()),
          static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()),true);
        snapshot["command_resume"]={{"version",1},{"sid",config.sid},{"last_admitted_id",controllee->admitted_message_id()},{"association_generation",controllee->association_generation()}};
        {std::lock_guard lock(status_cache.mutex);status_cache.value=std::move(snapshot);++status_cache.sequence;}
        status_cache.updated.notify_all();
      }
      report();
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    report(true);
    status_workers.clear();
    std::cout << sdr::radio_status(config, id, control.snapshot(),
                                   device->settings(), device->sample_ordinal(),
                     device->clipped_samples(), device->streaming(),
                     static_cast<std::uint64_t>(
                       std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - started)
                         .count()),
                     static_cast<std::uint64_t>(
                       std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count()),
                     false)
                     .dump()
              << '\n';
    if (restart_requested.load()) {
      transport.instance->detach();
      close(listener.value);
      listener.value = -1;
      execvp(argv[0], argv);
      throw std::runtime_error("radio process re-exec failed");
    }
    return 0;
  } catch (const std::exception &) {
    try { sdr::telemetry::Reporter("radio",std::cout).failed(); } catch (...) {}
    std::cerr << "radio: application error (see documented configuration requirements)\n";
    return 1;
  }
}