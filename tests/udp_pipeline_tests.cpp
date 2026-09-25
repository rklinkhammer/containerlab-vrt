#include "udp_pipeline.hpp"

#include <vita/codec/prologue.hpp>
#include <vita/codec/samples.hpp>
#include <vita/fields/packet.hpp>
#include <vita/profiles/iq/profile.hpp>
#include <vita/profiles/iq/sdr.hpp>
#include <vita/runtime/context/publisher.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <numbers>

namespace {
using namespace vita;

sdr::udp::ProcessorConfig processor_config() {
  sdr::udp::ProcessorConfig config;
  for (std::uint32_t sid = 1; sid <= 4; ++sid)
    config.listeners[sid - 1] = {sid, {"127.0.0.1", 10000}};
  config.source_host = "127.0.0.1";
  config.destination = {"127.0.0.1", 10001};
  config.processing = {};
  config.receive_bytes = 9000;
  config.queue_limit = 8;
  return config;
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

std::vector<std::byte> signal_wire(std::uint8_t count, std::size_t pairs,
                                   std::size_t offset = 0) {
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
} // namespace

int main() {
  sdr::udp::ProcessorCore processor(processor_config());
  const auto context = context_wire();
  assert(processor.consume(1, context).empty());
  assert(processor.consume(1, signal_wire(0, 1024)).empty());
  const auto complete = processor.consume(1, signal_wire(1, 1024, 1024));
  assert(complete.size() == 1);
  const auto spectrum = sdr::decode_spectrum(complete.front());
  assert(spectrum.stream_id == 1 && spectrum.ordinal == 0);
  const auto frequency = sdr::detect_frequency(spectrum);
  assert(frequency && std::abs(*frequency - 100'049'804.6875) < 1.0);

  sdr::udp::DetectorCore detector(
      {{"127.0.0.1", 10001}, 2048, sdr::spectrum_wire_size(2048)});
  const auto detection = detector.consume(complete.front());
  assert(detection && detection->validity == "valid" &&
         detection->frequency_hz);
  assert(!detector.consume(std::array<std::byte, 4>{}));

  sdr::udp::ProcessorCore gapped(processor_config());
  assert(gapped.consume(1, context).empty());
  assert(gapped.consume(1, signal_wire(0, 512)).empty());
  const auto with_gap = gapped.consume(1, signal_wire(2, 1024, 1024));
  assert(with_gap.size() == 1);
  const auto invalid = detector.consume(with_gap.front());
  assert(invalid && invalid->validity == "gapped" &&
         !invalid->frequency_hz);
  detector.finish();
    detector.note_unavailable();
  assert(detector.metrics().malformed == 1 && detector.metrics().gapped == 1 &&
      detector.metrics().sequence_gaps == 1 &&
      detector.metrics().unavailable == 4);
}