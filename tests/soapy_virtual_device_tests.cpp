#include <sdr/soapy_virtual_device.hpp>

#include <SoapySDR/Constants.h>
#include <SoapySDR/Errors.h>
#include <SoapySDR/Formats.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

int main() {
  sdr::SoapyVirtualDevice device("radio-test", 100'050'000, 0.25, 0.0);
  assert(device.getNumChannels(SOAPY_SDR_RX) == 1);
  assert(device.getNumChannels(SOAPY_SDR_TX) == 0);
  device.setSampleRate(SOAPY_SDR_RX, 0, 1'000'000);
  device.setFrequency(SOAPY_SDR_RX, 0, 100'000'000);
  device.setBandwidth(SOAPY_SDR_RX, 0, 800'000);
  device.setGain(SOAPY_SDR_RX, 0, 0);

  auto *stream = device.setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16);
  std::array<std::int16_t, 8> samples{};
  void *buffers[]{samples.data()};
  int flags = 0;
  long long time_ns = -1;
  assert(device.readStream(stream, buffers, 4, flags, time_ns, 0) ==
         SOAPY_SDR_TIMEOUT);
  assert(device.activateStream(stream) == 0);
  assert(device.readStream(stream, buffers, 4, flags, time_ns, 0) == 4);
  assert(samples[0] == 8192 && samples[1] == 0);
  assert(time_ns == 0 && (flags & SOAPY_SDR_HAS_TIME));
  assert(device.sample_ordinal() == 4);
  assert(device.streaming());
  assert(device.deactivateStream(stream) == 0);
  assert(!device.streaming());
  device.closeStream(stream);

  sdr::SoapyVirtualDevice independent("radio-other", 100'050'000, 0.25,
                                      std::acos(-1.0) / 2.0);
  auto *other_stream = independent.setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16);
  assert(independent.activateStream(other_stream) == 0);
  samples.fill(0);
  assert(independent.readStream(other_stream, buffers, 1, flags, time_ns, 0) == 1);
  assert(samples[0] == 0 && samples[1] == 8192);
  assert(device.sample_ordinal() == 4);
  independent.closeStream(other_stream);
}