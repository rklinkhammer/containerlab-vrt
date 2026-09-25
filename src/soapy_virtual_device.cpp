#include <sdr/soapy_virtual_device.hpp>

#include <SoapySDR/Constants.h>
#include <SoapySDR/Errors.h>
#include <SoapySDR/Formats.hpp>

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace sdr {
struct SoapyVirtualDevice::RxStream {
  bool active{};
};

SoapyVirtualDevice::SoapyVirtualDevice(std::string radio_id,
                                       std::uint64_t signal_hz,
                                       double amplitude,
                                       double phase_radians)
    : radio_id_(std::move(radio_id)),
      radio_(signal_hz, amplitude, phase_radians) {
  radio_.configure({});
}

SoapyVirtualDevice::~SoapyVirtualDevice() = default;

std::string SoapyVirtualDevice::getDriverKey() const { return "containerlab-vrt"; }
std::string SoapyVirtualDevice::getHardwareKey() const { return "virtual-radio"; }
SoapySDR::Kwargs SoapyVirtualDevice::getHardwareInfo() const {
  return {{"radio_id", radio_id_}};
}

void SoapyVirtualDevice::require_rx(std::size_t channel, int direction) const {
  if (direction != SOAPY_SDR_RX || channel != 0)
    throw std::invalid_argument("virtual radio supports RX channel 0 only");
}

std::size_t SoapyVirtualDevice::getNumChannels(int direction) const {
  return direction == SOAPY_SDR_RX ? 1 : 0;
}

std::vector<std::string>
SoapyVirtualDevice::getStreamFormats(int direction, std::size_t channel) const {
  require_rx(channel, direction);
  return {SOAPY_SDR_CS16};
}

std::string SoapyVirtualDevice::getNativeStreamFormat(
    int direction, std::size_t channel, double &full_scale) const {
  require_rx(channel, direction);
  full_scale = 32767.0;
  return SOAPY_SDR_CS16;
}

void SoapyVirtualDevice::configure_locked(const RadioSettings &settings) {
  radio_.configure(settings);
}

void SoapyVirtualDevice::setSampleRate(int direction, std::size_t channel,
                                       double rate) {
  require_rx(channel, direction);
  if (!std::isfinite(rate) || rate < 0 || rate > UINT32_MAX)
    throw std::invalid_argument("invalid sample rate");
  std::lock_guard lock(mutex_);
  auto next = radio_.settings();
  next.sample_rate_hz = static_cast<std::uint32_t>(std::llround(rate));
  configure_locked(next);
}

double SoapyVirtualDevice::getSampleRate(int direction,
                                         std::size_t channel) const {
  require_rx(channel, direction);
  std::lock_guard lock(mutex_);
  return radio_.settings().sample_rate_hz;
}

void SoapyVirtualDevice::setFrequency(int direction, std::size_t channel,
                                      double frequency,
                                      const SoapySDR::Kwargs &) {
  require_rx(channel, direction);
  if (!std::isfinite(frequency) || frequency < 0 ||
      frequency > static_cast<double>(UINT64_MAX))
    throw std::invalid_argument("invalid center frequency");
  std::lock_guard lock(mutex_);
  auto next = radio_.settings();
  next.center_hz = static_cast<std::uint64_t>(std::llround(frequency));
  configure_locked(next);
}

double SoapyVirtualDevice::getFrequency(int direction,
                                        std::size_t channel) const {
  require_rx(channel, direction);
  std::lock_guard lock(mutex_);
  return static_cast<double>(radio_.settings().center_hz);
}

void SoapyVirtualDevice::setBandwidth(int direction, std::size_t channel,
                                      double bandwidth) {
  require_rx(channel, direction);
  if (!std::isfinite(bandwidth) || bandwidth < 0 || bandwidth > UINT32_MAX)
    throw std::invalid_argument("invalid bandwidth");
  std::lock_guard lock(mutex_);
  auto next = radio_.settings();
  next.bandwidth_hz = static_cast<std::uint32_t>(std::llround(bandwidth));
  configure_locked(next);
}

double SoapyVirtualDevice::getBandwidth(int direction,
                                        std::size_t channel) const {
  require_rx(channel, direction);
  std::lock_guard lock(mutex_);
  return radio_.settings().bandwidth_hz;
}

void SoapyVirtualDevice::setGain(int direction, std::size_t channel,
                                 double gain) {
  require_rx(channel, direction);
  std::lock_guard lock(mutex_);
  auto next = radio_.settings();
  next.gain_db = gain;
  configure_locked(next);
}

double SoapyVirtualDevice::getGain(int direction, std::size_t channel) const {
  require_rx(channel, direction);
  std::lock_guard lock(mutex_);
  return radio_.settings().gain_db;
}

SoapySDR::Stream *SoapyVirtualDevice::setupStream(
    int direction, const std::string &format,
    const std::vector<std::size_t> &channels, const SoapySDR::Kwargs &) {
  const auto channel = channels.empty() ? 0 : channels.front();
  require_rx(channel, direction);
  if (format != SOAPY_SDR_CS16 || channels.size() > 1)
    throw std::invalid_argument("virtual radio requires one CS16 RX channel");
  std::lock_guard lock(mutex_);
  if (stream_)
    throw std::runtime_error("RX stream already exists");
  stream_ = std::make_unique<RxStream>();
  return reinterpret_cast<SoapySDR::Stream *>(stream_.get());
}

SoapyVirtualDevice::RxStream &
SoapyVirtualDevice::require_stream(SoapySDR::Stream *stream) const {
  if (!stream_ || reinterpret_cast<void *>(stream) != stream_.get())
    throw std::invalid_argument("unknown RX stream");
  return *stream_;
}

void SoapyVirtualDevice::closeStream(SoapySDR::Stream *stream) {
  std::lock_guard lock(mutex_);
  static_cast<void>(require_stream(stream));
  radio_.stop();
  stream_.reset();
}

std::size_t SoapyVirtualDevice::getStreamMTU(SoapySDR::Stream *stream) const {
  std::lock_guard lock(mutex_);
  static_cast<void>(require_stream(stream));
  return 1024;
}

int SoapyVirtualDevice::activateStream(SoapySDR::Stream *stream, int flags,
                                       long long, std::size_t) {
  if (flags != 0)
    return SOAPY_SDR_NOT_SUPPORTED;
  std::lock_guard lock(mutex_);
  auto &rx = require_stream(stream);
  radio_.begin_epoch();
  radio_.start();
  rx.active = true;
  return 0;
}

int SoapyVirtualDevice::deactivateStream(SoapySDR::Stream *stream, int flags,
                                         long long) {
  if (flags != 0)
    return SOAPY_SDR_NOT_SUPPORTED;
  std::lock_guard lock(mutex_);
  auto &rx = require_stream(stream);
  rx.active = false;
  radio_.stop();
  return 0;
}

int SoapyVirtualDevice::readStream(SoapySDR::Stream *stream,
                                   void *const *buffers,
                                   std::size_t elements, int &flags,
                                   long long &time_ns, long timeout_us) {
  if (!buffers || !buffers[0] || elements == 0 || elements > 1024)
    return SOAPY_SDR_STREAM_ERROR;
  std::unique_lock lock(mutex_);
  auto &rx = require_stream(stream);
  if (!rx.active) {
    lock.unlock();
    if (timeout_us > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(timeout_us));
    return SOAPY_SDR_TIMEOUT;
  }
  const auto ordinal = radio_.sample_ordinal();
  auto *samples = static_cast<std::int16_t *>(buffers[0]);
  const auto count = radio_.read({samples, elements * 2}, elements);
  flags = SOAPY_SDR_HAS_TIME;
  time_ns = static_cast<long long>(ordinal * 1'000'000'000ULL /
                                   radio_.settings().sample_rate_hz);
  return static_cast<int>(count);
}

RadioSettings SoapyVirtualDevice::settings() const {
  std::lock_guard lock(mutex_);
  return radio_.settings();
}

void SoapyVirtualDevice::apply_settings(const RadioSettings &settings) {
  std::lock_guard lock(mutex_);
  configure_locked(settings);
}

std::uint64_t SoapyVirtualDevice::sample_ordinal() const {
  std::lock_guard lock(mutex_);
  return radio_.sample_ordinal();
}

std::uint64_t SoapyVirtualDevice::clipped_samples() const {
  std::lock_guard lock(mutex_);
  return radio_.clipped_samples();
}

bool SoapyVirtualDevice::streaming() const {
  std::lock_guard lock(mutex_);
  return radio_.streaming();
}
} // namespace sdr