#include <sdr/status.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::byte> request(std::string payload) {
  std::vector<std::byte> wire(payload.size() + 4);
  for (std::size_t index = 0; index < 4; ++index)
    wire[index] = std::byte((payload.size() >> (8 * (3 - index))) & 0xff);
  for (std::size_t index = 0; index < payload.size(); ++index)
    wire[index + 4] = std::byte{static_cast<unsigned char>(payload[index])};
  return wire;
}

void framing_contract() {
  const auto wire = request(R"({"version":1,"operation":"get_status"})");
  sdr::StatusFrameDecoder decoder;
  for (std::byte value : wire)
    assert(decoder.feed({&value, 1}));
  assert(decoder.complete());
  const auto parsed = decoder.take_request();
  assert(parsed["version"] == 1);

  sdr::StatusFrameDecoder extra;
  auto duplicated = wire;
  duplicated.insert(duplicated.end(), wire.begin(), wire.end());
  assert(!extra.feed(duplicated) && extra.failed());

  sdr::StatusFrameDecoder oversized;
  const std::array<std::byte, 4> header{std::byte{0}, std::byte{0}, std::byte{4}, std::byte{1}};
  assert(!oversized.feed(header) && oversized.failed());

  sdr::StatusFrameDecoder malformed;
  const auto invalid = request("{");
  assert(malformed.feed(invalid));
  bool rejected = false;
  try {
    static_cast<void>(malformed.take_request());
  } catch (const nlohmann::json::exception &) {
    rejected = true;
  }
  assert(rejected);

  auto response = sdr::encode_status_response({{"version", 1}, {"ready", true}});
  assert(response.size() > 4);
}

void ownership_contract() {
  using namespace std::chrono_literals;
  sdr::ControlSlot slot;
  const auto start = sdr::ControlSlot::Clock::now();
  const auto first = slot.acquire(start);
  assert(first && *first == 1);
  assert(!slot.acquire(start));
  assert(!slot.observe_liveness(2, start + 1s));
  assert(slot.observe_liveness(*first, start + 1s));
  assert(!slot.release_if_stale(start + 5s, 5s));
  assert(slot.release_if_stale(start + 6s, 5s));
  const auto second = slot.acquire(start + 6s);
  assert(second && *second == 2);
  assert(!slot.release(*first));
  assert(slot.release(*second));
}
} // namespace

int main() {
  framing_contract();
  ownership_contract();
}