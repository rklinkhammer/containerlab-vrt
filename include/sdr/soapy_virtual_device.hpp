#pragma once

#include <SoapySDR/Device.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <sdr/virtual_radio.hpp>

namespace sdr {
class SoapyVirtualDevice final : public SoapySDR::Device {
public:
  SoapyVirtualDevice(std::string radio_id, std::uint64_t signal_hz,
                     double amplitude, double phase_radians);
  ~SoapyVirtualDevice() override;

  std::string getDriverKey() const override;
  std::string getHardwareKey() const override;
  SoapySDR::Kwargs getHardwareInfo() const override;
  std::size_t getNumChannels(int direction) const override;
  std::vector<std::string> getStreamFormats(int direction,
                                             std::size_t channel) const override;
  std::string getNativeStreamFormat(int direction, std::size_t channel,
                                    double &full_scale) const override;

  void setSampleRate(int direction, std::size_t channel, double rate) override;
  double getSampleRate(int direction, std::size_t channel) const override;
  void setFrequency(int direction, std::size_t channel, double frequency,
                    const SoapySDR::Kwargs &args = {}) override;
  double getFrequency(int direction, std::size_t channel) const override;
  void setBandwidth(int direction, std::size_t channel, double bandwidth) override;
  double getBandwidth(int direction, std::size_t channel) const override;
  void setGain(int direction, std::size_t channel, double gain) override;
  double getGain(int direction, std::size_t channel) const override;

  SoapySDR::Stream *setupStream(
      int direction, const std::string &format,
      const std::vector<std::size_t> &channels = {},
      const SoapySDR::Kwargs &args = {}) override;
  void closeStream(SoapySDR::Stream *stream) override;
  std::size_t getStreamMTU(SoapySDR::Stream *stream) const override;
  int activateStream(SoapySDR::Stream *stream, int flags = 0,
                     long long time_ns = 0, std::size_t elements = 0) override;
  int deactivateStream(SoapySDR::Stream *stream, int flags = 0,
                       long long time_ns = 0) override;
  int readStream(SoapySDR::Stream *stream, void *const *buffers,
                 std::size_t elements, int &flags, long long &time_ns,
                 long timeout_us = 100000) override;

  RadioSettings settings() const;
  void apply_settings(const RadioSettings &settings);
  std::uint64_t sample_ordinal() const;
  std::uint64_t clipped_samples() const;
  bool streaming() const;

private:
  struct RxStream;
  void require_rx(std::size_t channel, int direction) const;
  RxStream &require_stream(SoapySDR::Stream *stream) const;
  void configure_locked(const RadioSettings &settings);

  std::string radio_id_;
  mutable std::mutex mutex_;
  VirtualRadio radio_;
  std::unique_ptr<RxStream> stream_;
};
} // namespace sdr