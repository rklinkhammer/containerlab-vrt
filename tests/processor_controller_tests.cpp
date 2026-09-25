#include <sdr/processor_controller.hpp>
#include <sdr/radio_services.hpp>
#include <sdr/radio_transport.hpp>
#include <sdr/vrt_radio_adapter.hpp>

#include <vita/codec/packet.hpp>
#include <vita/profiles/iq/lab.hpp>
#include <vita/runtime/public/runtime.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
using Runtime = vita::VitaRuntime<1, 16, 256, 1024 * 1024>;

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

void nonblocking(int descriptor) {
  const auto flags = fcntl(descriptor, F_GETFL, 0);
  assert(flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0);
}

Descriptor tcp_listener(std::uint16_t &port) {
  Descriptor result(socket(AF_INET, SOCK_STREAM, 0));
  assert(result.value >= 0);
  const int reuse = 1;
  assert(setsockopt(result.value, SOL_SOCKET, SO_REUSEADDR, &reuse,
                    sizeof(reuse)) == 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(result.value, reinterpret_cast<sockaddr *>(&address),
              sizeof(address)) == 0);
  assert(listen(result.value, 4) == 0);
  socklen_t length = sizeof(address);
  assert(getsockname(result.value, reinterpret_cast<sockaddr *>(&address),
                     &length) == 0);
  port = ntohs(address.sin_port);
  nonblocking(result.value);
  return result;
}

Descriptor udp_listener(std::uint16_t &port) {
  Descriptor result(socket(AF_INET, SOCK_DGRAM, 0));
  assert(result.value >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(result.value, reinterpret_cast<sockaddr *>(&address),
              sizeof(address)) == 0);
  socklen_t length = sizeof(address);
  assert(getsockname(result.value, reinterpret_cast<sockaddr *>(&address),
                     &length) == 0);
  port = ntohs(address.sin_port);
  nonblocking(result.value);
  return result;
}

vita::runtime::timing::ProtocolTime utc_now() {
  const auto elapsed = std::chrono::system_clock::now().time_since_epoch();
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
  return {static_cast<std::uint64_t>(seconds.count()),
          static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed - seconds)
                  .count()) *
              1000};
}

struct RadioFixture {
  std::string id;
  std::uint32_t sid{};
  std::uint16_t iq_port{};
  std::uint16_t status_port{};
  Descriptor iq{udp_listener(iq_port)};
  Descriptor status{tcp_listener(status_port)};
  sdr::ControlSlot control;
  sdr::RadioTransportFactory transport;
  std::shared_ptr<sdr::SoapyVirtualDevice> device;
  std::shared_ptr<sdr::VrtRadioAdapter> adapter;
  std::unique_ptr<Runtime> runtime;
  std::optional<Runtime::Controllee> controllee;
  Clock::time_point origin{Clock::now()};
  vita::runtime::timing::ProtocolTime clock_origin{utc_now()};
  std::atomic<bool> stop{false};
  std::jthread worker;

  RadioFixture(std::string radio_id, std::uint32_t stream_id)
      : id(std::move(radio_id)), sid(stream_id),
        transport({.control_host = "127.0.0.1",
                   .control_port = 0,
                   .data_source_host = "127.0.0.1",
                   .data_host = "127.0.0.1",
                   .data_port = iq_port,
                   .sid = sid,
                   .control_queue = 64},
                  &control, {}),
        device(std::make_shared<sdr::SoapyVirtualDevice>(
            id, 100'000'000 + sid * 50'000, 0.25, 0.0)),
        adapter(sdr::VrtRadioAdapter::create(device)) {
    sdr::RadioSettings initial;
    initial.center_hz = 99'000'000;
    initial.sample_rate_hz = 500'000;
    initial.bandwidth_hz = 400'000;
    device->apply_settings(initial);
    auto config = vita::profiles::iq::lab::config(
        vita::profiles::iq::sdr_unknown_oui);
    vita::profiles::iq::lab::PoolCounts counts;
    counts.payload_bytes = 4096;
    counts.rx_data_bytes = 8192;
    auto pools = vita::profiles::iq::lab::pools(counts);
    assert(config && pools);
    config->clock.epoch = vita::runtime::timing::Epoch::utc;
    config->timing.device_late_ps = 100'000'000'000ULL;
    config->transport = sdr::radio_transport_factory(transport);
    auto made = Runtime::create(*config, std::move(*pools));
    assert(made);
    runtime = std::move(*made);
    vita::StreamConfig stream;
    stream.sid = sid;
    stream.controller_id = 1;
    stream.controllee_id = 2;
    stream.profile = vita::profiles::iq::Profile::sdr_radio;
    stream.role = vita::EndpointRole::controllee_only;
    stream.sample_rate = 500'000;
    stream.center_frequency = 99'000'000;
    stream.bandwidth = 400'000;
    stream.ip_mtu = 9000;
    stream.maximum_samples_per_packet = 1024;
    stream.burst_pairs = 262144;
    stream.trailer = true;
    stream.source = adapter->source_provider();
    stream.device = adapter->device_binding();
    auto added = runtime->add_controllee(stream);
    assert(added);
    controllee = *added;
    assert(runtime->observe_pps({0}, clock_origin));
    worker = std::jthread([this] { run(); });
  }

  ~RadioFixture() {
    stop.store(true);
    if (worker.joinable())
      worker.join();
  }

  void run() {
    std::uint64_t last_second = 0;
    while (!stop.load()) {
      const auto elapsed = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - origin)
              .count());
      const auto second = elapsed / 1'000'000'000;
      if (second > last_second) {
        auto pulse_time = clock_origin;
        pulse_time.seconds += second;
        assert(runtime->observe_pps({second * 1'000'000'000}, pulse_time));
        last_second = second;
      }
      auto progressed = runtime->progress({elapsed});
      if (!progressed && !progressed.error().retryable)
        std::cerr << id << " progress_error="
                  << static_cast<unsigned>(progressed.error().code)
                  << " stage="
                  << static_cast<unsigned>(progressed.error().stage)
                  << " offset=" << progressed.error().offset << '\n';
      assert(progressed || progressed.error().retryable);
      pollfd item{status.value, POLLIN, 0};
      if (poll(&item, 1, 0) == 1 && (item.revents & POLLIN)) {
        Descriptor client(accept(status.value, nullptr, nullptr));
        if (client.value >= 0) {
          sdr::RadioConfig config;
          config.id = id;
          config.sid = sid;
          const auto snapshot = sdr::radio_status(
              config, "boot-" + id, control.snapshot(), device->settings(),
              device->sample_ordinal(), device->clipped_samples(),
              device->streaming(), elapsed / 1'000'000,
              static_cast<std::uint64_t>(
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count()),
              true);
          assert(sdr::serve_status_transaction(client.value, snapshot));
        }
      }
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }

  std::optional<vita::runtime::timing::ProtocolTime>
  first_signal(Clock::time_point deadline) {
    std::array<std::byte, 8192> bytes{};
    while (Clock::now() < deadline) {
      const auto count = recv(iq.value, bytes.data(), bytes.size(), 0);
      if (count > 0) {
        auto envelope = vita::codec::decode_envelope(
            vita::Bytes{bytes}.first(static_cast<std::size_t>(count)));
        if (envelope && vita::codec::is_data(envelope->envelope.type))
          return vita::runtime::timing::ProtocolTime{
              envelope->envelope.timestamp.integer,
              envelope->envelope.timestamp.fractional};
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
  }
};
} // namespace

int main() {
  std::array<std::unique_ptr<RadioFixture>, 4> radios;
  sdr::ProcessorControllerConfig config;
  config.io_timeout = std::chrono::milliseconds(500);
  config.maximum_backoff = std::chrono::milliseconds(200);
  config.liveness_interval = std::chrono::milliseconds(50);
  for (std::uint32_t sid = 1; sid <= 4; ++sid) {
    radios[sid - 1] =
        std::make_unique<RadioFixture>("radio" + std::to_string(sid), sid);
    config.radios[sid - 1] = {
        .id = "radio" + std::to_string(sid),
        .sid = sid,
        .control_host = "127.0.0.1",
        .control_port = radios[sid - 1]->transport.instance->control_port(),
        .status_host = "127.0.0.1",
        .status_port = radios[sid - 1]->status_port,
        .center_hz = 100'000'000,
        .sample_rate_hz = 1'000'000,
        .bandwidth_hz = 800'000,
        .gain_db_q7 = 0};
  }

  std::ostringstream events;
  sdr::ProcessorController controller(config, events);
  const auto admission_deadline = Clock::now() + std::chrono::seconds(4);
  while (!controller.coordinated() && Clock::now() < admission_deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  if (!controller.coordinated()) {
    const auto metrics = controller.metrics();
    std::cerr << events.str() << "attempts=" << metrics.connection_attempts
              << " connections=" << metrics.connections
              << " status_failures=" << metrics.status_failures
              << " protocol_failures=" << metrics.protocol_failures
              << " configurations=" << metrics.configurations << '\n';
    for (const auto &radio : radios) {
      const auto transport = radio->transport.instance->metrics();
      std::cerr << radio->id << " accepted=" << transport.accepted_connections
                << " rx=" << transport.received_packets
                << " tx=" << transport.completed_submissions
                << " failed=" << transport.failed_submissions << '\n';
    }
  }
  assert(controller.coordinated());
  for (const auto &radio : radios) {
    const auto settings = radio->device->settings();
    assert(settings.center_hz == 100'000'000);
    assert(settings.sample_rate_hz == 1'000'000);
    assert(settings.bandwidth_hz == 800'000);
  }

  const auto data_deadline = Clock::now() + std::chrono::seconds(6);
  std::array<vita::runtime::timing::ProtocolTime, 4> epochs{};
  for (std::size_t index = 0; index < radios.size(); ++index) {
    auto epoch = radios[index]->first_signal(data_deadline);
    if (!epoch)
      std::cerr << events.str() << radios[index]->id
                << " streaming=" << radios[index]->device->streaming()
                << " source_status="
                << static_cast<unsigned>(radios[index]->controllee->status())
                << " ordinal=" << radios[index]->device->sample_ordinal()
                << '\n';
    assert(epoch);
    epochs[index] = *epoch;
  }
  for (std::size_t index = 1; index < epochs.size(); ++index)
    assert(epochs[index] == epochs[0]);

  const auto before = controller.metrics();
  assert(before.starts_submitted == 4 && before.starts_admitted == 4);
  radios[0]->transport.instance->disconnect_controller();
  const auto reconnect_deadline = Clock::now() + std::chrono::seconds(2);
  while (controller.metrics().reconnects == before.reconnects &&
         Clock::now() < reconnect_deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  const auto after = controller.metrics();
  assert(after.reconnects > before.reconnects);
  assert(after.starts_submitted == 4);
  assert(after.stale_starts_replayed == 0);

  const auto sustained_deadline = Clock::now() + std::chrono::seconds(5);
  while (Clock::now() < sustained_deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  const auto sustained = controller.metrics();
  assert(sustained.protocol_failures == after.protocol_failures);
  assert(sustained.starts_submitted == 4);
}