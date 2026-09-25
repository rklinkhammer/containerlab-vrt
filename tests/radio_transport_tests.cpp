#include <sdr/radio_services.hpp>
#include <sdr/radio_transport.hpp>
#include <sdr/vrt_radio_adapter.hpp>

#include <vita/codec/packet.hpp>
#include <vita/profiles/iq/lab.hpp>
#include <vita/runtime/public/runtime.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {
using Runtime = vita::VitaRuntime<1, 16, 256, 1024 * 1024>;

struct Socket {
  int descriptor{-1};
  explicit Socket(int value = -1) : descriptor(value) {}
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

void set_nonblocking(int descriptor) {
  const auto flags = fcntl(descriptor, F_GETFL, 0);
  assert(flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0);
}

Socket udp_receiver(std::uint16_t &port) {
  Socket result(socket(AF_INET, SOCK_DGRAM, 0));
  assert(result.descriptor >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(result.descriptor, reinterpret_cast<sockaddr *>(&address),
              sizeof(address)) == 0);
  socklen_t length = sizeof(address);
  assert(getsockname(result.descriptor, reinterpret_cast<sockaddr *>(&address),
                     &length) == 0);
  port = ntohs(address.sin_port);
  set_nonblocking(result.descriptor);
  return result;
}

Socket connect_tcp(std::uint16_t port) {
  Socket result(socket(AF_INET, SOCK_STREAM, 0));
  assert(result.descriptor >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(connect(result.descriptor, reinterpret_cast<sockaddr *>(&address),
                 sizeof(address)) == 0);
  set_nonblocking(result.descriptor);
  return result;
}

std::vector<std::byte> command(std::uint32_t message_id, std::uint32_t cam,
                               bool configure,
                               std::optional<std::uint32_t> action = {}) {
  vita::codec::Envelope envelope;
  envelope.type = vita::codec::PacketType::command;
  envelope.stream_id = 1;
  envelope.command = vita::codec::Command{
      cam, message_id, vita::codec::Identifier::short_id(2),
      vita::codec::Identifier::short_id(1)};
  envelope.timestamp = {vita::codec::Tsi::utc,
                        vita::codec::Tsf::picoseconds, 1000, 0};
  if (action && *action == 3)
    envelope.timestamp = {vita::codec::Tsi::utc,
                          vita::codec::Tsf::picoseconds, 1000,
                          50'000'000'000};
  vita::ControlPacket packet;
  packet.configure(vita::profiles::iq::command_class(
                       vita::profiles::iq::Profile::sdr_radio),
                   configure || action ? 2 : 0);
  if (configure) {
    assert(packet.set<vita::Bandwidth>(*vita::Hertz::from_integer(800'000)));
    assert(packet.set<vita::RFReferenceFrequency>(
        *vita::Hertz::from_integer(100'000'000)));
    assert(packet.set<vita::Gain>(vita::GainStages{0, 0}));
    assert(packet.set<vita::SampleRate>(
        *vita::Hertz::from_integer(1'000'000)));
  } else if (action) {
    assert(packet.set<vita::DiscreteIO32>(*action));
  }
  std::array<std::byte, 1024> output{};
  auto encoded = vita::codec::encode_packet(envelope, packet.freeze(), output);
  assert(encoded);
  return {output.begin(), output.begin() + static_cast<std::ptrdiff_t>(*encoded)};
}

std::vector<std::byte> status_query(std::uint32_t message_id) {
  vita::codec::Envelope envelope;
  envelope.type = vita::codec::PacketType::command;
  envelope.stream_id = 1;
  envelope.command = vita::codec::Command{
      0xa01f0000, message_id, vita::codec::Identifier::short_id(2),
      vita::codec::Identifier::short_id(1)};
  envelope.timestamp = {vita::codec::Tsi::utc,
                        vita::codec::Tsf::picoseconds, 1000, 0};
  vita::QueryPacket packet;
  assert(packet.select(vita::RFReferenceFrequency::id));
  assert(packet.select(vita::SampleRate::id));
  assert(packet.select(vita::Bandwidth::id));
  assert(packet.select(vita::Gain::id));
  assert(packet.select(vita::DiscreteIO32::id));
  std::array<std::byte, 1024> output{};
  auto encoded = vita::codec::encode_packet(envelope, packet.freeze(), output);
  assert(encoded);
  return {output.begin(), output.begin() + static_cast<std::ptrdiff_t>(*encoded)};
}

bool send_all(int descriptor, std::span<const std::byte> bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto sent = send(descriptor, bytes.data() + offset,
                           bytes.size() - offset, 0);
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      std::this_thread::yield();
      continue;
    }
    if (sent <= 0)
      return false;
    offset += static_cast<std::size_t>(sent);
  }
  return true;
}

struct Fixture {
  std::uint16_t data_port{};
  Socket data{udp_receiver(data_port)};
  sdr::ControlSlot control;
  sdr::RadioTransportFactory transport{
      {.control_host = "127.0.0.1",
       .control_port = 0,
        .data_source_host = "127.0.0.1",
       .data_host = "127.0.0.1",
       .data_port = data_port,
       .sid = 1,
       .control_queue = 64,
       .tcp_send_buffer = 1024,
      .maximum_tcp_write_bytes = 7,
       .io_timeout = std::chrono::seconds(2),
       .stale_timeout = std::chrono::milliseconds(120)},
      &control,
      {}};
  std::shared_ptr<sdr::SoapyVirtualDevice> device{
      std::make_shared<sdr::SoapyVirtualDevice>("test", 100'050'000, 0.25,
                                                 0.0)};
  std::shared_ptr<sdr::VrtRadioAdapter> adapter{
      sdr::VrtRadioAdapter::create(device)};
  std::unique_ptr<Runtime> runtime;
  std::optional<Runtime::Controllee> controllee;
  std::uint64_t now{};

  Fixture() {
    sdr::RadioSettings defaults;
    device->apply_settings(defaults);
    auto config = vita::profiles::iq::lab::config(
        vita::profiles::iq::sdr_unknown_oui);
    vita::profiles::iq::lab::PoolCounts counts;
    counts.payload_bytes = 4096;
    counts.rx_data_bytes = 8192;
    auto pools = vita::profiles::iq::lab::pools(counts);
    assert(config && pools);
    config->clock.epoch = vita::runtime::timing::Epoch::utc;
    config->transport = sdr::radio_transport_factory(transport);
    auto made = Runtime::create(*config, std::move(*pools));
    assert(made);
    runtime = std::move(*made);
    vita::StreamConfig stream;
    stream.sid = 1;
    stream.controller_id = 1;
    stream.controllee_id = 2;
    stream.profile = vita::profiles::iq::Profile::sdr_radio;
    stream.role = vita::EndpointRole::controllee_only;
    stream.sample_rate = 1'000'000;
    stream.bandwidth = 800'000;
    stream.ip_mtu = 9000;
    stream.maximum_samples_per_packet = 1024;
    stream.burst_pairs = 262144;
    stream.trailer = true;
    stream.source = adapter->source_provider();
    stream.device = adapter->device_binding();
    auto added = runtime->add_controllee(stream);
    assert(added);
    controllee = *added;
    assert(runtime->observe_pps({0}, {1000, 0}));
  }

  void pump(std::uint64_t until, bool sleep = false) {
    while (now < until) {
      now += 100'000;
      auto progressed = runtime->progress({now});
      assert(progressed || progressed.error().retryable);
      if (sleep)
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }
};

void drain(int descriptor) {
  std::array<std::byte, 8192> bytes{};
  while (recv(descriptor, bytes.data(), bytes.size(), 0) > 0) {
  }
}

void runtime_control_path() {
  Fixture fixture;
  Socket controller(connect_tcp(fixture.transport.instance->control_port()));
  fixture.pump(2'000'000, true);
  assert(fixture.transport.instance->connected());

  std::array<std::byte, 8192> datagram{};
  fixture.pump(10'000'000);
  assert(recv(fixture.data.descriptor, datagram.data(), datagram.size(), 0) < 0 &&
         (errno == EAGAIN || errno == EWOULDBLOCK));

  auto configured = command(1, 0xa11f0000, true);
  assert(send_all(controller.descriptor,
                  std::span<const std::byte>{configured}.first(3)));
  fixture.pump(12'000'000, true);
  assert(send_all(controller.descriptor,
                  std::span<const std::byte>{configured}.subspan(3)));
  fixture.pump(20'000'000, true);
  drain(controller.descriptor);
  assert(fixture.device->settings().sample_rate_hz == 1'000'000);

  auto started = command(2, 0xa11f1000, false, 3);
  auto status = status_query(3);
  std::vector<std::byte> coalesced(started);
  coalesced.insert(coalesced.end(), status.begin(), status.end());
  assert(send_all(controller.descriptor, coalesced));
  fixture.pump(45'000'000, true);
  assert(!fixture.device->streaming());
  assert(recv(fixture.data.descriptor, datagram.data(), datagram.size(), 0) < 0);
  drain(controller.descriptor);
  fixture.pump(70'000'000, true);
  assert(fixture.device->streaming());
  const auto received =
      recv(fixture.data.descriptor, datagram.data(), datagram.size(), 0);
  assert(received > 0);
  auto decoded = vita::codec::decode_packet(
      vita::Bytes{datagram}.first(static_cast<std::size_t>(received)));
    assert(decoded &&
       decoded->envelope.envelope.type == vita::codec::PacketType::context);
    fixture.pump(75'000'000, true);
      bool found_signal = false;
      for (std::size_t attempt = 0; attempt < 8 && !found_signal; ++attempt) {
      const auto signal_received =
        recv(fixture.data.descriptor, datagram.data(), datagram.size(), 0);
      assert(signal_received > 0);
    decoded = vita::codec::decode_packet(
        vita::Bytes{datagram}.first(static_cast<std::size_t>(signal_received)));
    assert(decoded);
    found_signal = vita::codec::is_data(decoded->envelope.envelope.type);
  }
  assert(found_signal);

  Socket rejected(connect_tcp(fixture.transport.instance->control_port()));
  fixture.pump(77'000'000, true);
  std::byte probe{};
  assert(recv(rejected.descriptor, &probe, 1, 0) == 0);

  const auto state_version = fixture.device->settings();
  controller = Socket{};
  fixture.pump(79'000'000, true);
  Socket reconnected(connect_tcp(fixture.transport.instance->control_port()));
  fixture.pump(81'000'000, true);
  assert(fixture.transport.instance->connected());
  assert(fixture.device->settings().sample_rate_hz ==
         state_version.sample_rate_hz);
  assert(fixture.device->streaming());

  const std::array<std::byte, 4> malformed{};
  assert(send_all(reconnected.descriptor, malformed));
  fixture.pump(83'000'000, true);
  assert(!fixture.transport.instance->connected());
  assert(recv(reconnected.descriptor, &probe, 1, 0) == 0);
}

void backpressure_and_stale_status_isolation() {
  Fixture fixture;
  Socket controller(connect_tcp(fixture.transport.instance->control_port()));
  fixture.pump(2'000'000, true);
  bool backpressured = false;
  for (std::uint32_t message = 1; message <= 220 && !backpressured; ++message) {
    const auto query = status_query(message);
    assert(send_all(controller.descriptor, query));
    fixture.pump(fixture.now + 1'000'000, true);
    const auto metrics = fixture.transport.instance->metrics();
    backpressured = metrics.partial_writes || metrics.write_retries;
  }
  assert(backpressured);
  drain(controller.descriptor);
  fixture.pump(fixture.now + 5'000'000, true);

  controller = Socket{};
  fixture.pump(fixture.now + 2'000'000, true);
  Socket stale(connect_tcp(fixture.transport.instance->control_port()));
  fixture.pump(fixture.now + 2'000'000, true);
  const auto generation = fixture.control.snapshot().generation;
  assert(generation);

  std::array<int, 2> status_sockets{};
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, status_sockets.data()) == 0);
  std::thread service([&] {
    const nlohmann::json status{{"version", 1}};
    assert(sdr::serve_status_transaction(status_sockets[0], status));
    close(status_sockets[0]);
  });
  const std::string payload = R"({"version":1,"operation":"get_status"})";
  std::vector<std::byte> request(payload.size() + 4);
  for (std::size_t index = 0; index < 4; ++index)
    request[index] =
        std::byte((payload.size() >> (8 * (3 - index))) & 0xff);
  std::memcpy(request.data() + 4, payload.data(), payload.size());
  assert(send_all(status_sockets[1], request));
  std::array<std::byte, 256> response{};
  assert(recv(status_sockets[1], response.data(), response.size(), 0) > 0);
  close(status_sockets[1]);
  service.join();

  std::this_thread::sleep_for(std::chrono::milliseconds(140));
  fixture.pump(fixture.now + 1'000'000, true);
  assert(!fixture.transport.instance->connected());
  assert(!fixture.control.snapshot().occupied);
}
} // namespace

int main() {
  runtime_control_path();
  backpressure_and_stale_status_isolation();
}