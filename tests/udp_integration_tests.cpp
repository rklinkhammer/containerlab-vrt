#include "udp_pipeline.hpp"

#include <vita/codec/prologue.hpp>
#include <vita/codec/samples.hpp>
#include <vita/fields/packet.hpp>
#include <vita/profiles/iq/profile.hpp>
#include <vita/profiles/iq/sdr.hpp>
#include <vita/runtime/context/publisher.hpp>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <sstream>
#include <thread>
#include <vector>

namespace {
using namespace vita;
std::atomic_bool stop_requested{};

bool stopping() noexcept { return stop_requested.load(); }

std::array<std::uint16_t, 5> free_ports() {
  std::array<int, 5> descriptors{};
  std::array<std::uint16_t, 5> ports{};
  for (std::size_t index = 0; index < descriptors.size(); ++index) {
    descriptors[index] = socket(AF_INET, SOCK_DGRAM, 0);
    assert(descriptors[index] >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(descriptors[index], reinterpret_cast<sockaddr *>(&address),
                sizeof(address)) == 0);
    socklen_t length = sizeof(address);
    assert(getsockname(descriptors[index], reinterpret_cast<sockaddr *>(&address),
                       &length) == 0);
    ports[index] = ntohs(address.sin_port);
  }
  for (const int descriptor : descriptors)
    close(descriptor);
  return ports;
}

std::vector<std::byte> context_wire() {
  runtime::context::ContextFrame frame;
  frame.state.profile = profiles::iq::Profile::sdr_radio;
  frame.state.fields[runtime::field_index(Bandwidth::id)] = {
      Bandwidth::id, *Hertz::from_integer(800'000), runtime::Validity::known};
  frame.state.fields[runtime::field_index(RFReferenceFrequency::id)] = {
      RFReferenceFrequency::id, *Hertz::from_integer(100'000'000),
      runtime::Validity::known};
  frame.state.fields[runtime::field_index(Gain::id)] = {
      Gain::id, GainStages{}, runtime::Validity::known};
  frame.state.fields[runtime::field_index(SampleRate::id)] = {
      SampleRate::id, *Hertz::from_integer(1'000'000), runtime::Validity::known};
  frame.time = {1000, 0};
  frame.epoch = codec::Tsi::utc;
  frame.time_known = true;
  frame.valid = true;
  codec::Envelope envelope;
  envelope.type = codec::PacketType::context;
  envelope.stream_id = 1;
  std::vector<std::byte> wire(256);
  const auto size = runtime::context::encode_context(frame, envelope, wire);
  assert(size);
  wire.resize(*size);
  return wire;
}

std::vector<std::byte> signal_wire(std::uint8_t count, std::size_t offset) {
  constexpr std::size_t pairs = 1024;
  std::vector<codec::Iq<std::int16_t>> samples(pairs);
  for (std::size_t index = 0; index < pairs; ++index) {
    const auto phase = 2 * std::numbers::pi * 102 * (offset + index) / 2048;
    samples[index] = {static_cast<std::int16_t>(std::cos(phase) * 16000),
                      static_cast<std::int16_t>(std::sin(phase) * 16000)};
  }
  std::vector<std::byte> payload(pairs * 4);
  assert(codec::pack_iq16(samples, payload));
  codec::Envelope envelope;
  envelope.type = codec::PacketType::signal;
  envelope.stream_id = 1;
  envelope.class_id = codec::ClassId{profiles::iq::sdr_unknown_oui, 0, 0};
  envelope.timestamp = {codec::Tsi::utc, codec::Tsf::picoseconds, 1000,
                        offset * 1'000'000};
  envelope.packet_count = count;
  envelope.trailer = true;
  std::vector<std::byte> wire(payload.size() + 64);
  const auto size = codec::encode_envelope(
      envelope, payload,
      profiles::iq::sdr_trailer(profiles::iq::SampleFrame::middle), wire);
  assert(size);
  wire.resize(*size);
  return wire;
}

void send_datagram(std::uint16_t port, const std::vector<std::byte> &wire) {
  const int descriptor = socket(AF_INET, SOCK_DGRAM, 0);
  assert(descriptor >= 0);
  sockaddr_in destination{};
  destination.sin_family = AF_INET;
  destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  destination.sin_port = htons(port);
  assert(sendto(descriptor, wire.data(), wire.size(), 0,
                reinterpret_cast<sockaddr *>(&destination),
                sizeof(destination)) == static_cast<ssize_t>(wire.size()));
  close(descriptor);
}
} // namespace

int main() {
  const auto ports = free_ports();
  sdr::udp::DetectorConfig detector_config{
      {"127.0.0.1", ports[4]}, 2048, sdr::spectrum_wire_size(2048)};
  sdr::udp::ProcessorConfig processor_config;
  for (std::uint32_t sid = 1; sid <= 4; ++sid)
    processor_config.listeners[sid - 1] = {
        sid, {"127.0.0.1", ports[sid - 1]}};
  processor_config.source_host = "127.0.0.1";
  processor_config.destination = detector_config.listener;
  processor_config.processing = {};
  processor_config.receive_bytes = 9000;
  processor_config.queue_limit = 8;

  std::ostringstream detector_output;
  std::ostringstream processor_output;
  std::thread detector_thread([&] {
    assert(sdr::udp::run_detector(detector_config,
                                  std::chrono::milliseconds(400),
                                  detector_output) == 0);
  });
  std::thread processor_thread([&] {
    assert(sdr::udp::run_processor(processor_config,
                                   std::chrono::milliseconds(300),
                                   processor_output) == 0);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  send_datagram(ports[0], context_wire());
  send_datagram(ports[0], signal_wire(0, 0));
  send_datagram(ports[0], signal_wire(1, 1024));
  processor_thread.join();
  detector_thread.join();

  assert(processor_output.str().find("\"spectra\":1") != std::string::npos);
  assert(processor_output.str().find("\"malformed\":0") != std::string::npos);
  assert(detector_output.str().find("\"type\":\"detection\"") !=
         std::string::npos);
  assert(detector_output.str().find("\"validity\":\"valid\"") !=
         std::string::npos);
  assert(detector_output.str().find("\"detections\":1") != std::string::npos);
  assert(detector_output.str().find("\"unavailable\":3") != std::string::npos);

  std::ostringstream shutdown_output;
  const auto shutdown_started = std::chrono::steady_clock::now();
  std::thread shutdown_thread([&] {
    assert(sdr::udp::run_detector(detector_config, std::nullopt, shutdown_output,
                                  stopping) == 0);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  stop_requested.store(true);
  shutdown_thread.join();
  assert(std::chrono::steady_clock::now() - shutdown_started <
         std::chrono::seconds(2));
}