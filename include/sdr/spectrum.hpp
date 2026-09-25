#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <sdr/time.hpp>

namespace sdr {
struct ProcessingConfig {
  std::uint32_t sample_rate_hz{1'000'000};
  std::uint32_t fft_size{2048};
  std::uint32_t overlap_samples{};
  void validate() const;
  std::uint32_t hop() const noexcept { return fft_size - overlap_samples; }
};

struct Spectrum {
  std::uint32_t stream_id{};
  std::uint32_t sequence{};
  std::uint32_t sample_rate_hz{};
  std::uint32_t fft_size{};
  std::uint32_t hop{};
  std::uint64_t center_hz{};
  ProtocolTime begin{};
  ProtocolTime end{};
  ProtocolTime epoch{};
  std::uint64_t ordinal{};
  std::uint64_t discontinuities{};
  std::uint64_t sequence_gaps{};
  std::vector<std::uint8_t> gaps;
  std::vector<float> power;
};

std::size_t spectrum_wire_size(std::size_t fft_size);
Spectrum power_spectrum(const ProcessingConfig &config,
                        std::span<const std::complex<double>> samples,
                        std::span<const std::uint8_t> missing);
std::vector<std::byte> encode_spectrum(const Spectrum &spectrum);
Spectrum decode_spectrum(std::span<const std::byte> wire);
std::optional<double> detect_frequency(const Spectrum &spectrum);
} // namespace sdr
