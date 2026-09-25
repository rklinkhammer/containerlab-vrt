#include <sdr/virtual_radio.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace sdr {
VirtualRadio::VirtualRadio(std::uint64_t signal_hz, double amplitude,
                           double phase_radians)
    : signal_hz_(signal_hz), amplitude_(amplitude), phase_(phase_radians),
      initial_phase_(phase_radians) {
  if (signal_hz < 1'000'000 || signal_hz > 6'000'000'000ULL ||
      !std::isfinite(amplitude) || amplitude < 0 || amplitude > 1 ||
      !std::isfinite(phase_radians) || phase_radians < 0 ||
      phase_radians > 2 * std::numbers::pi)
    throw std::invalid_argument("invalid virtual radio signal");
}

void VirtualRadio::validate(const RadioSettings &settings) {
  if (settings.center_hz < 1'000'000 ||
      settings.center_hz > 6'000'000'000ULL ||
      settings.sample_rate_hz < 1'000 || settings.sample_rate_hz > 2'000'000 ||
      settings.bandwidth_hz < 1 ||
      settings.bandwidth_hz > settings.sample_rate_hz ||
      !std::isfinite(settings.gain_db) || settings.gain_db < -60 ||
      settings.gain_db > 60)
    throw std::invalid_argument("unsupported radio setting");
}

void VirtualRadio::configure(const RadioSettings &settings) {
  validate(settings);
  settings_ = settings;
}

void VirtualRadio::begin_epoch() noexcept {
  sample_ordinal_ = 0;
  phase_ = initial_phase_;
}

void VirtualRadio::start() noexcept { streaming_ = true; }
void VirtualRadio::stop() noexcept { streaming_ = false; }

void VirtualRadio::skip_samples(std::uint64_t count) noexcept {
  const long double cycles =
      static_cast<long double>(static_cast<std::int64_t>(signal_hz_) -
                               static_cast<std::int64_t>(settings_.center_hz)) *
      count / settings_.sample_rate_hz;
  phase_ = std::remainder(
      phase_ + static_cast<double>(std::remainder(cycles, 1.0L)) *
                   2 * std::numbers::pi,
      2 * std::numbers::pi);
  sample_ordinal_ += count;
}

std::size_t VirtualRadio::read(std::span<std::int16_t> interleaved_iq,
                               std::size_t pairs) {
  if (!streaming_ || pairs == 0 || pairs > 1024 ||
      interleaved_iq.size() < pairs * 2)
    return 0;
  const auto offset = static_cast<double>(signal_hz_) - settings_.center_hz;
  const bool in_band =
      std::abs(offset) <= settings_.bandwidth_hz / 2.0 &&
      std::abs(offset) < settings_.sample_rate_hz / 2.0;
  const double scale = in_band
                           ? 32767.0 * amplitude_ *
                                 std::pow(10.0, settings_.gain_db / 20.0)
                           : 0.0;
  const double increment =
      2 * std::numbers::pi *
      std::remainder(offset, static_cast<double>(settings_.sample_rate_hz)) /
      settings_.sample_rate_hz;
  const auto convert = [this](double value) {
    if (value < -32768 || value > 32767)
      ++clipped_samples_;
    return static_cast<std::int16_t>(
        std::llround(std::clamp(value, -32768.0, 32767.0)));
  };
  for (std::size_t index = 0; index < pairs; ++index) {
    interleaved_iq[index * 2] = convert(scale * std::cos(phase_));
    interleaved_iq[index * 2 + 1] = convert(scale * std::sin(phase_));
    phase_ = std::remainder(phase_ + increment, 2 * std::numbers::pi);
  }
  sample_ordinal_ += pairs;
  return pairs;
}
} // namespace sdr
