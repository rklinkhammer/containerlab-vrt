#include <sdr/radio_services.hpp>

#include <vita/codec/packet.hpp>
#include <vita/codec/prologue.hpp>

#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <thread>
#include <vector>

namespace {
std::vector<std::byte> status_request() {
  const std::string payload = R"({"version":1,"operation":"get_status"})";
  std::vector<std::byte> wire(payload.size() + 4);
  for (std::size_t index = 0; index < 4; ++index)
    wire[index] = std::byte((payload.size() >> (8 * (3 - index))) & 0xff);
  std::memcpy(wire.data() + 4, payload.data(), payload.size());
  return wire;
}

void status_socket_contract() {
  std::array<int, 2> sockets{};
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()) == 0);
  const nlohmann::json expected{{"version", 1}, {"boot_id", "boot-test"}};
  std::thread server([&] {
    assert(sdr::serve_status_transaction(sockets[0], expected));
    close(sockets[0]);
  });
  const auto request = status_request();
  for (std::byte value : request)
    assert(send(sockets[1], &value, 1, 0) == 1);
  std::array<std::byte, 256> response{};
  const auto count = recv(sockets[1], response.data(), response.size(), 0);
  assert(count > 4);
  sdr::StatusFrameDecoder decoder;
  assert(decoder.feed({response.data(), static_cast<std::size_t>(count)}));
  const auto payload_size = decoder.expected_payload_bytes();
  const auto json = nlohmann::json::parse(
      reinterpret_cast<const char *>(response.data() + 4),
      reinterpret_cast<const char *>(response.data() + 4 + payload_size));
  assert(json == expected);
  close(sockets[1]);
  server.join();
}

void framing_and_ownership_contract() {
  using namespace std::chrono_literals;
  vita::codec::Envelope envelope;
  envelope.type = vita::codec::PacketType::context;
  envelope.stream_id = 1;
  std::array<std::byte, 256> wire{};
  const auto bytes = vita::codec::encode_prologue(envelope, 0, wire);
  assert(bytes);
  std::vector<std::byte> delivered;
  sdr::VrtControlFramer framer(256, 1, [&](std::span<const std::byte> value) {
    delivered.assign(value.begin(), value.end());
    return true;
  });
  for (std::size_t index = 0; index < *bytes; ++index)
    assert(framer.feed({wire.data() + index, 1}));
  assert(framer.packets() == 1 && delivered.size() == *bytes);
  assert(framer.disconnect());

  sdr::ControlSlot slot;
  const auto start = sdr::ControlSlot::Clock::time_point{};
  const auto generation = slot.acquire(start);
  assert(generation && !slot.acquire(start));
  assert(slot.observe_liveness(*generation, start + 900ms));
  assert(!slot.release_if_stale(start + 1800ms, 1s));
  assert(slot.release_if_stale(start + 2s, 1s));
  const auto next = slot.acquire(start + 2s);
  assert(next && *next != *generation);
}
} // namespace

int main() {
  const auto config = sdr::load_radio_config("../../generated/radio1.json");
  assert(config.id == "radio1" && config.control_port == 18401);
    assert(config.application_host == "0.0.0.0" &&
      config.data_source_host == "10.79.0.10" &&
         config.application_mtu == 9000);
  status_socket_contract();
  framing_and_ownership_contract();
}