#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>
#include <vita/codec/packet.hpp>
#include <vita/codec/prologue.hpp>
#include <vita/profiles/iq/graphx.hpp>
#include <vita/profiles/iq/profile.hpp>
#include <vita/runtime/context/publisher.hpp>
#include <vita/runtime/transaction/outcomes.hpp>

using namespace vita;
using namespace vita::codec;
using namespace vita::profiles::iq;

namespace {

template <std::size_t Size>
auto bytes(const std::array<std::uint32_t, Size> &words) {
  std::array<std::byte, Size * 4> result{};
  for (std::size_t index = 0; index < Size; ++index) {
    for (unsigned offset = 0; offset < 4; ++offset) {
      result[index * 4 + offset] =
          std::byte(words[index] >> (24 - 8 * offset));
    }
  }
  return result;
}

template <std::size_t Pairs>
auto signal(std::uint32_t header, SampleFrame frame) {
  std::array<std::uint32_t, Pairs + 8> packet{};
  packet[0] = header;
  packet[1] = 1;
  packet[2] = 0x00ffffff;
  packet[4] = 1000;
  for (std::size_t index = 0; index < Pairs; ++index) {
    packet[7 + index] = 0x7fff8000;
  }
  packet.back() =
      0x00c00000u | (static_cast<std::uint32_t>(frame) << 10);
  return bytes(packet);
}

void emit(std::string_view name, std::span<const std::byte> value) {
  std::cout << name << ' ';
  for (const auto byte : value) {
    std::cout << std::hex << std::setfill('0') << std::setw(2)
              << std::to_integer<unsigned>(byte);
  }
  std::cout << '\n';
}

template <std::size_t Size>
void verify_and_emit(std::string_view name,
                     const std::array<std::byte, Size> &value,
                     std::optional<RequestContext> request = std::nullopt) {
  assert(decode_packet(value, DecodeOptions{request}));
  emit(name, value);
}

template <std::size_t Pairs>
void emit_signal(std::string_view name, std::uint32_t header,
                 SampleFrame frame) {
  const auto expected = signal<Pairs>(header, frame);
  const auto decoded = decode_envelope(expected);
  assert(decoded && decoded->trailer &&
         *decoded->trailer == graphx_trailer(frame));

  Envelope envelope;
  envelope.type = PacketType::signal;
  envelope.stream_id = 1;
  envelope.class_id = ClassId{graphx_unknown_oui, 0, 0};
  envelope.timestamp = {Tsi::utc, Tsf::picoseconds, 1000, 0};
  envelope.trailer = true;
  envelope.packet_count = static_cast<std::uint8_t>((header >> 16) & 0xf);
  std::array<std::byte, 28> actual{};
  const auto size = encode_prologue(envelope, Pairs * 4, actual);
  assert(size && *size == actual.size() &&
         std::equal(actual.begin(), actual.end(), expected.begin()));
  emit(name, expected);
}

} // namespace

int main() {
  emit_signal<1>("signal-single", 0x1c600009, SampleFrame::single);
  emit_signal<2>("signal-first", 0x1c61000a, SampleFrame::first);
  emit_signal<1023>("signal-middle", 0x1c620407, SampleFrame::middle);
  emit_signal<1024>("signal-final", 0x1c630408, SampleFrame::final);

  const auto context = bytes(std::array<std::uint32_t, 13>{
      0x4060000d, 1, 1000, 0, 0, 0x28a00000, 0x000000c3, 0x50000000,
      0x00005f5e, 0x10000000, 0x00000500, 0x000000f4, 0x24000000});
  const auto decoded_context = decode_packet(context);
  assert(decoded_context && decoded_context->fields.size() == 4);
  runtime::context::ContextFrame frame;
  frame.state.profile = Profile::graphx_radio;
  frame.time = {1000, 0};
  frame.epoch = Tsi::utc;
  frame.time_known = true;
  frame.valid = true;
  frame.change = false;
  for (std::size_t index = 0; index < decoded_context->fields.size(); ++index) {
    const auto &field = decoded_context->fields[index];
    frame.state.fields[runtime::field_index(field.id)] = {
        field.id, *field.value(), runtime::Validity::known};
  }
  Envelope context_envelope;
  context_envelope.type = PacketType::context;
  context_envelope.stream_id = 1;
  std::array<std::byte, 256> actual_context{};
  const auto context_size = runtime::context::encode_context(
      frame, context_envelope, actual_context);
  assert(context_size && *context_size == context.size() &&
         std::equal(context.begin(), context.end(), actual_context.begin()));
  emit("context-current", context);

  verify_and_emit("configure", bytes(std::array<std::uint32_t, 17>{
                                   0x60600011, 1, 1000, 0, 0, 0xa11f0000,
                                   0x11223344, 1, 1, 0x28a00000, 0x000000c3,
                                   0x50000000, 0x00005f5e, 0x10000000,
                                   0x00000500, 0x000001e8, 0x48000000}));
  verify_and_emit("start-scheduled", bytes(std::array<std::uint32_t, 12>{
                                         0x6060000c, 1, 1000, 0x0000000b,
                                         0xa43b7400, 0xa11f1000, 0x11223345,
                                         1, 1, 2, 64, 3}));
  verify_and_emit("stop", bytes(std::array<std::uint32_t, 12>{
                              0x6060000c, 1, 1000, 0, 0, 0xa11f0000,
                              0x11223346, 1, 1, 2, 64, 2}));
  verify_and_emit("status-query", bytes(std::array<std::uint32_t, 11>{
                                      0x6060000b, 1, 1000, 0, 0, 0xa0040000,
                                      0x11223344, 1, 2, 2, 0x40}));

  constexpr RequestContext graphx_request{0xa11f0000};
  verify_and_emit("execution-ack", bytes(std::array<std::uint32_t, 9>{
                                       0x64630009, 1, 1000, 0, 0, 0xa1080400,
                                       0x11223344, 1, 2}),
                  graphx_request);
  verify_and_emit("status-ack", bytes(std::array<std::uint32_t, 12>{
                                    0x6464000c, 1, 1000, 0, 1, 0xa1040000,
                                    0x11223344, 1, 2, 2, 0x40, 3}),
                  graphx_request);
  verify_and_emit("diagnostic-ack", bytes(std::array<std::uint32_t, 11>{
                                        0x6465000b, 1, 1000, 0, 0, 0xa1110000,
                                        0x11223344, 1, 2, 0x20000000,
                                        0x90000000}),
                  graphx_request);

  const auto capability_query = bytes(std::array<std::uint32_t, 11>{
      0x6060000b, 1, 1000, 0, 0, 0xa0040000, 0x11223344, 1, 2, 0x28a00080,
      0x0c000000});
  verify_and_emit("capability-query", capability_query);
  const auto capability_response = bytes(std::array<std::uint32_t, 25>{
      0x64600019, 1,          1000,       0,          0,
      0xa0040400, 0x11223344, 1,          2,          0x28a00080,
      0x0c000000, 0x000001e8, 0x48000000, 0,          0x00100000,
      0x00165a0b, 0xc0000000, 0x000000f4, 0x24000000, 0x00001e00,
      0x0000e200, 0x000001e8, 0x48000000, 0,          0x3e800000});
  const auto request = decode_packet(capability_query);
  assert(request);
  profiles::iq::GraphxCapabilities supported;
  runtime::transaction::AckRecord ranges;
  ranges.request = request->envelope.envelope;
  ranges.cam = *runtime::transaction::Cam::parse(
      ranges.request, runtime::transaction::Profile::graphx_radio);
  ranges.kind = runtime::transaction::AckKind::state;
  ranges.selected_mask = 0x72;
  ranges.graphx_capabilities = &supported;
  ranges.time_known = true;
  ranges.epoch = Tsi::utc;
  ranges.time = {1000, 0};
  ranges.scheduled_or_executed = true;
  std::array<std::byte, 256> actual_response{};
  const auto response_size =
      runtime::transaction::encode_response(ranges, actual_response);
  assert(response_size && *response_size == capability_response.size() &&
         std::equal(capability_response.begin(), capability_response.end(),
                    actual_response.begin()));
  verify_and_emit("capability-response", capability_response,
                  RequestContext{0xa0040000});
}
