#include <sdr/spectrum.hpp>
#include <sdr/virtual_radio.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
void virtual_radio_contract() {
  sdr::VirtualRadio radio(100'050'000, 0.25, 0);
  radio.configure({});
  std::array<std::int16_t, 8> samples{};
  assert(radio.read(samples, 4) == 0);
  radio.begin_epoch();
  radio.start();
  assert(radio.read(samples, 4) == 4);
  assert(samples[0] == 8192 && samples[1] == 0);
  assert(radio.sample_ordinal() == 4);
  radio.skip_samples(996);
  assert(radio.sample_ordinal() == 1000);
  radio.stop();
  assert(radio.read(samples, 4) == 0);
}

void spectrum_contract() {
  constexpr std::size_t size = 2048;
  constexpr std::size_t positive_bin = 102;
  std::vector<std::complex<double>> samples(size);
  std::vector<std::uint8_t> missing(size);
  for (std::size_t index = 0; index < size; ++index)
    samples[index] = std::polar(1.0, 2 * std::numbers::pi * positive_bin * index / size);
  auto spectrum = sdr::power_spectrum({}, samples, missing);
  spectrum.stream_id = 3;
  spectrum.sequence = 7;
  spectrum.center_hz = 100'000'000;
  spectrum.begin = {1000, 0};
  spectrum.end = {1000, 2'048'000'000};
  spectrum.epoch = spectrum.begin;
  const auto detected = sdr::detect_frequency(spectrum);
  assert(detected && std::abs(*detected - 100'049'804.6875) < 0.001);
  assert(std::abs(*detected - 100'050'000.0) <= 244.140625);

  const auto wire = sdr::encode_spectrum(spectrum);
  assert(wire.size() == 8576);
  const auto decoded = sdr::decode_spectrum(wire);
  assert(decoded.stream_id == 3 && decoded.sequence == 7);
  assert(sdr::detect_frequency(decoded) == detected);

  auto zero = spectrum;
  std::fill(zero.power.begin(), zero.power.end(), 0);
  assert(!sdr::detect_frequency(zero));
  auto gapped = spectrum;
  gapped.gaps[0] = 0x80;
  assert(!sdr::detect_frequency(gapped));

  auto malformed = wire;
  malformed[0] = std::byte{'X'};
  bool rejected = false;
  try {
    static_cast<void>(sdr::decode_spectrum(malformed));
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
}
} // namespace

int main() {
  virtual_radio_contract();
  spectrum_contract();
}
