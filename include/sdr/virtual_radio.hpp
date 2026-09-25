#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sdr {
struct RadioSettings {
  std::uint64_t center_hz{100'000'000};
  std::uint32_t sample_rate_hz{1'000'000};
  std::uint32_t bandwidth_hz{800'000};
  double gain_db{};
};

class VirtualRadio {
public:
  VirtualRadio(std::uint64_t signal_hz, double amplitude,
               double phase_radians);

  void configure(const RadioSettings &settings);
  void begin_epoch() noexcept;
  void start() noexcept;
  void stop() noexcept;
  void skip_samples(std::uint64_t count) noexcept;
  std::size_t read(std::span<std::int16_t> interleaved_iq,
                   std::size_t pairs);

  const RadioSettings &settings() const noexcept { return settings_; }
  std::uint64_t sample_ordinal() const noexcept { return sample_ordinal_; }
  std::uint64_t clipped_samples() const noexcept { return clipped_samples_; }
  bool streaming() const noexcept { return streaming_; }

private:
  static void validate(const RadioSettings &settings);
  std::uint64_t signal_hz_;
  double amplitude_;
  double phase_;
  double initial_phase_;
  RadioSettings settings_{};
  std::uint64_t sample_ordinal_{};
  std::uint64_t clipped_samples_{};
  bool streaming_{};
};
} // namespace sdr
