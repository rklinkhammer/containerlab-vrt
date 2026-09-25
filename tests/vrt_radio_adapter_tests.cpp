#include <sdr/vrt_radio_adapter.hpp>

#include <vita/profiles/iq/source.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <memory>

namespace {
vita::runtime::PlannedField field(vita::FieldId id,
                                  vita::SemanticValue value) {
  return {.id = id,
          .requested = value,
          .adjusted = std::move(value),
          .eligible = true,
          .time_known = true,
          .ordinal_known = true};
}
} // namespace

int main() {
  auto device = std::make_shared<sdr::SoapyVirtualDevice>(
      "radio-test", 100'050'000, 0.25, 0.0);
  auto adapter = sdr::VrtRadioAdapter::create(device);
  auto backend = adapter->device_binding();
  assert(backend.valid() && backend.backend.commit);

  vita::runtime::ExecutionPlan configure;
  configure.count = 4;
  configure.fields[0] = field(vita::SampleRate::id,
                              *vita::Hertz::from_integer(1'000'000));
  configure.fields[1] = field(vita::RFReferenceFrequency::id,
                              *vita::Hertz::from_integer(100'000'000));
  configure.fields[2] = field(vita::Bandwidth::id,
                              *vita::Hertz::from_integer(800'000));
  configure.fields[3] = field(vita::Gain::id, vita::GainStages{0, 0});
  auto outcome = backend.backend.commit(backend.backend.context, configure, {});
  assert(outcome.status == vita::runtime::FieldStatus::executed);
  assert(device->settings().bandwidth_hz == 800'000);

  vita::runtime::ExecutionPlan start;
  start.count = 1;
  start.fields[0] = field(vita::DiscreteIO32::id, std::uint32_t{3});
  outcome = backend.backend.commit(backend.backend.context, start, {});
  assert(outcome.status == vita::runtime::FieldStatus::executed);
  assert(device->streaming());

  std::array<std::byte, 16> payload{};
  auto window = vita::profiles::iq::SampleWriteWindow::create(
      payload, vita::profiles::iq::SampleFormat::iq16, 0, 4, {});
  assert(window);
  auto source = adapter->source_provider();
  assert(source.produce(*window));
  assert(device->sample_ordinal() == 4);
  assert(std::to_integer<unsigned>(payload[0]) == 0x20);

  vita::runtime::ExecutionPlan stop;
  stop.count = 1;
  stop.fields[0] = field(vita::DiscreteIO32::id, std::uint32_t{2});
  outcome = backend.backend.commit(backend.backend.context, stop, {});
  assert(outcome.status == vita::runtime::FieldStatus::executed);
  assert(!device->streaming());
}