#include <sdr/spectrum.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace sdr {
namespace {
constexpr std::uint32_t magic = 0x53445231;
constexpr std::uint16_t version = 1;
constexpr std::size_t header_bytes = 128;

void require(bool condition) {
  if (!condition)
    throw std::invalid_argument("invalid SDR spectrum");
}

void put(std::vector<std::byte> &output, std::size_t offset,
         std::uint64_t value, std::size_t count) {
  require(offset + count <= output.size());
  for (std::size_t index = 0; index < count; ++index)
    output[offset + index] =
        std::byte((value >> (8 * (count - index - 1))) & 0xff);
}

std::uint64_t get(std::span<const std::byte> input, std::size_t offset,
                  std::size_t count) {
  require(offset + count <= input.size());
  std::uint64_t result = 0;
  for (std::size_t index = 0; index < count; ++index)
    result = (result << 8) | std::to_integer<unsigned>(input[offset + index]);
  return result;
}

std::uint64_t missing_count(const Spectrum &spectrum) {
  std::uint64_t count = 0;
  for (std::size_t index = 0; index < spectrum.fft_size; ++index)
    count += (spectrum.gaps[index / 8] >> (7 - index % 8)) & 1u;
  return count;
}

void validate_spectrum(const Spectrum &spectrum) {
  ProcessingConfig{.sample_rate_hz = spectrum.sample_rate_hz,
                   .fft_size = spectrum.fft_size,
                   .overlap_samples = spectrum.fft_size - spectrum.hop}
      .validate();
  require(spectrum.stream_id >= 1 && spectrum.stream_id <= 4);
  require(spectrum.center_hz >= 1'000'000 &&
          spectrum.center_hz <= 6'000'000'000ULL);
  require(spectrum.gaps.size() == spectrum.fft_size / 8);
  require(spectrum.power.size() == spectrum.fft_size);
  for (float value : spectrum.power)
    require(std::isfinite(value) && value >= 0);
}
} // namespace

void ProcessingConfig::validate() const {
  require(sample_rate_hz >= 1'000 && sample_rate_hz <= 2'000'000);
  require(fft_size >= 64 && fft_size <= 2048 && std::has_single_bit(fft_size));
  require(overlap_samples < fft_size &&
          (overlap_samples == 0 || overlap_samples * 2 == fft_size ||
           overlap_samples * 4 == fft_size * 3));
}

std::size_t spectrum_wire_size(std::size_t fft_size) {
  require(fft_size >= 64 && fft_size <= 2048 && std::has_single_bit(fft_size));
  return header_bytes + fft_size / 8 + 4 * fft_size;
}

Spectrum power_spectrum(const ProcessingConfig &config,
                        std::span<const std::complex<double>> samples,
                        std::span<const std::uint8_t> missing) {
  config.validate();
  require(samples.size() == config.fft_size && missing.size() == config.fft_size);
  Spectrum result;
  result.sample_rate_hz = config.sample_rate_hz;
  result.fft_size = config.fft_size;
  result.hop = config.hop();
  result.gaps.resize(config.fft_size / 8);
  result.power.resize(config.fft_size);
  std::vector<std::complex<double>> values(config.fft_size);
  for (std::size_t index = 0; index < config.fft_size; ++index) {
    require(std::isfinite(samples[index].real()) &&
            std::isfinite(samples[index].imag()));
    if (missing[index])
      result.gaps[index / 8] |= std::uint8_t(1u << (7 - index % 8));
    else
      values[index] = samples[index];
  }
  for (std::size_t index = 1, reversed = 0; index < config.fft_size; ++index) {
    std::size_t bit = config.fft_size >> 1;
    for (; reversed & bit; bit >>= 1)
      reversed ^= bit;
    reversed ^= bit;
    if (index < reversed)
      std::swap(values[index], values[reversed]);
  }
  for (std::size_t length = 2; length <= config.fft_size; length *= 2) {
    const auto root = std::polar(1.0, -2 * std::numbers::pi / length);
    for (std::size_t base = 0; base < config.fft_size; base += length) {
      std::complex<double> factor = 1;
      for (std::size_t offset = 0; offset < length / 2; ++offset) {
        const auto even = values[base + offset];
        const auto odd = factor * values[base + offset + length / 2];
        values[base + offset] = even + odd;
        values[base + offset + length / 2] = even - odd;
        factor *= root;
      }
    }
  }
  const double scale = static_cast<double>(config.fft_size) * config.fft_size;
  for (std::size_t index = 0; index < config.fft_size; ++index)
    result.power[index] = static_cast<float>(
        std::norm(values[(index + config.fft_size / 2) % config.fft_size]) /
        scale);
  return result;
}

std::vector<std::byte> encode_spectrum(const Spectrum &spectrum) {
  validate_spectrum(spectrum);
  std::vector<std::byte> output(spectrum_wire_size(spectrum.fft_size));
  put(output, 0, magic, 4);
  put(output, 4, version, 2);
  put(output, 6, header_bytes, 2);
  put(output, 8, output.size(), 4);
  put(output, 12, spectrum.stream_id, 4);
  put(output, 16, spectrum.sequence, 4);
  put(output, 20, spectrum.fft_size, 4);
  put(output, 24, spectrum.hop, 4);
  put(output, 28, 0, 4);
  put(output, 32, spectrum.center_hz, 8);
  put(output, 40, spectrum.sample_rate_hz, 8);
  put(output, 48, spectrum.begin.seconds, 8);
  put(output, 56, spectrum.begin.picoseconds, 8);
  put(output, 64, spectrum.end.seconds, 8);
  put(output, 72, spectrum.end.picoseconds, 8);
  const auto missing = missing_count(spectrum);
  put(output, 80, spectrum.fft_size - missing, 4);
  put(output, 84, missing, 4);
  put(output, 88, spectrum.discontinuities, 8);
  put(output, 96, spectrum.epoch.seconds, 8);
  put(output, 104, spectrum.epoch.picoseconds, 8);
  put(output, 112, spectrum.ordinal, 8);
  put(output, 120, spectrum.sequence_gaps, 8);
  for (std::size_t index = 0; index < spectrum.gaps.size(); ++index)
    output[header_bytes + index] = std::byte{spectrum.gaps[index]};
  const auto power_offset = header_bytes + spectrum.gaps.size();
  for (std::size_t index = 0; index < spectrum.power.size(); ++index)
    put(output, power_offset + index * 4,
        std::bit_cast<std::uint32_t>(spectrum.power[index]), 4);
  return output;
}

Spectrum decode_spectrum(std::span<const std::byte> wire) {
  require(wire.size() >= header_bytes && get(wire, 0, 4) == magic &&
          get(wire, 4, 2) == version && get(wire, 6, 2) == header_bytes &&
          get(wire, 8, 4) == wire.size() && get(wire, 28, 4) == 0);
  Spectrum result;
  result.stream_id = get(wire, 12, 4);
  result.sequence = get(wire, 16, 4);
  result.fft_size = get(wire, 20, 4);
  result.hop = get(wire, 24, 4);
  require(wire.size() == spectrum_wire_size(result.fft_size));
  result.center_hz = get(wire, 32, 8);
  result.sample_rate_hz = get(wire, 40, 8);
  result.begin = {get(wire, 48, 8), get(wire, 56, 8)};
  result.end = {get(wire, 64, 8), get(wire, 72, 8)};
  result.discontinuities = get(wire, 88, 8);
  result.epoch = {get(wire, 96, 8), get(wire, 104, 8)};
  result.ordinal = get(wire, 112, 8);
  result.sequence_gaps = get(wire, 120, 8);
  result.gaps.resize(result.fft_size / 8);
  for (std::size_t index = 0; index < result.gaps.size(); ++index)
    result.gaps[index] = std::to_integer<std::uint8_t>(wire[header_bytes + index]);
  result.power.resize(result.fft_size);
  const auto power_offset = header_bytes + result.gaps.size();
  for (std::size_t index = 0; index < result.power.size(); ++index)
    result.power[index] = std::bit_cast<float>(
        static_cast<std::uint32_t>(get(wire, power_offset + index * 4, 4)));
  validate_spectrum(result);
  const auto missing = missing_count(result);
  require(get(wire, 80, 4) == result.fft_size - missing &&
          get(wire, 84, 4) == missing);
  return result;
}

std::optional<double> detect_frequency(const Spectrum &spectrum) {
  validate_spectrum(spectrum);
  if (missing_count(spectrum) != 0)
    return std::nullopt;
  const auto peak = std::max_element(spectrum.power.begin(), spectrum.power.end());
  if (peak == spectrum.power.end() || *peak == 0)
    return std::nullopt;
  return static_cast<double>(spectrum.center_hz) +
         (std::distance(spectrum.power.begin(), peak) -
          static_cast<double>(spectrum.fft_size) / 2) *
             spectrum.sample_rate_hz / spectrum.fft_size;
}
} // namespace sdr
