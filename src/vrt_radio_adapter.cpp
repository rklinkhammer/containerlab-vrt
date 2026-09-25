#include <sdr/vrt_radio_adapter.hpp>

#include <SoapySDR/Constants.h>
#include <SoapySDR/Formats.hpp>
#include <vita/runtime/transaction/cam.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace sdr {
std::shared_ptr<VrtRadioAdapter>
VrtRadioAdapter::create(std::shared_ptr<SoapyVirtualDevice> device) {
  if (!device)
    throw std::invalid_argument("radio device is required");
  return std::shared_ptr<VrtRadioAdapter>(new VrtRadioAdapter(std::move(device)));
}

VrtRadioAdapter::VrtRadioAdapter(std::shared_ptr<SoapyVirtualDevice> device)
    : device_(std::move(device)),
      stream_(device_->setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16)) {}

VrtRadioAdapter::~VrtRadioAdapter() {
  if (stream_) {
    static_cast<void>(device_->deactivateStream(stream_));
    device_->closeStream(stream_);
  }
}

vita::DeviceBackendBinding VrtRadioAdapter::device_binding() {
  vita::runtime::transaction::Backend backend;
  backend.context = this;
  backend.validate = validate;
  backend.begin = begin;
  backend.quiescence = quiescence;
  backend.commit = commit;
  return {backend, shared_from_this(), sizeof(VrtRadioAdapter), nullptr};
}

vita::profiles::iq::SourceProvider VrtRadioAdapter::source_provider() noexcept {
  return {this, produce, nullptr};
}

vita::runtime::transaction::Validation
VrtRadioAdapter::validate(void *, vita::FieldId id, vita::SemanticValue value,
                          const vita::runtime::StateSnapshot &) noexcept {
  return vita::runtime::transaction::sdr_validate(id, std::move(value));
}

vita::Result<void> VrtRadioAdapter::begin(
    void *context, const vita::runtime::PlannedField &field,
    vita::runtime::timing::Boundary boundary,
    vita::runtime::transaction::AsyncResult result) noexcept {
  auto &self = *static_cast<VrtRadioAdapter *>(context);
  vita::runtime::FieldOutcome outcome;
  outcome.id = field.id;
  outcome.status = vita::runtime::FieldStatus::executed;
  outcome.value = field.adjusted;
  outcome.validity = vita::runtime::Validity::known;
  outcome.actual_time = boundary.time;
  outcome.sample_ordinal = boundary.sample_ordinal;
  outcome.time_known = field.time_known;
  outcome.ordinal_known = field.ordinal_known;
  if (field.id == vita::DiscreteIO32::id) {
    const auto *action = std::get_if<std::uint32_t>(&field.adjusted);
    const auto status = !action ? -1
                        : *action == 3
                            ? self.device_->activateStream(self.stream_)
                            : *action == 2
                                ? self.device_->deactivateStream(self.stream_)
                                : -1;
    if (status != 0)
      outcome.status = vita::runtime::FieldStatus::failed;
  }
  if (!result.complete(std::move(outcome)))
    return std::unexpected(vita::Error{vita::ErrorCode::invalid_state});
  return {};
}

vita::runtime::transaction::BackendQuiescence
VrtRadioAdapter::quiescence(void *) noexcept {
  return {.known = true, .quiescent = true, .pending = 0};
}

vita::runtime::transaction::BatchOutcome VrtRadioAdapter::commit(
    void *context, const vita::runtime::ExecutionPlan &plan,
    vita::runtime::timing::Boundary boundary) noexcept {
  auto &self = *static_cast<VrtRadioAdapter *>(context);
  auto settings = self.device_->settings();
  std::optional<std::uint32_t> stream_action;
  try {
    for (std::size_t index = 0; index < plan.count; ++index) {
      const auto &field = plan.fields[index];
      if (!field.eligible)
        continue;
      if (field.id == vita::SampleRate::id)
        settings.sample_rate_hz = static_cast<std::uint32_t>(
            std::get<vita::Hertz>(field.adjusted).q20 >> 20);
      else if (field.id == vita::RFReferenceFrequency::id)
        settings.center_hz = static_cast<std::uint64_t>(
            std::get<vita::Hertz>(field.adjusted).q20 >> 20);
      else if (field.id == vita::Bandwidth::id)
        settings.bandwidth_hz = static_cast<std::uint32_t>(
            std::get<vita::Hertz>(field.adjusted).q20 >> 20);
      else if (field.id == vita::Gain::id)
        settings.gain_db =
            std::get<vita::GainStages>(field.adjusted).stage1_q7 / 128.0;
      else if (field.id == vita::DiscreteIO32::id)
        stream_action = std::get<std::uint32_t>(field.adjusted);
    }
    self.device_->apply_settings(settings);
    if (stream_action == 3 && self.device_->activateStream(self.stream_) != 0)
      throw std::runtime_error("stream activation failed");
    if (stream_action == 2 && self.device_->deactivateStream(self.stream_) != 0)
      throw std::runtime_error("stream deactivation failed");
    return {.status = vita::runtime::FieldStatus::executed,
            .actual_time = boundary.time,
            .time_known = true,
            .uncertainty_ps = 0};
  } catch (...) {
    return {.status = vita::runtime::FieldStatus::failed};
  }
}

vita::Result<void> VrtRadioAdapter::produce(
    void *context, vita::profiles::iq::SampleWriteWindow &window) noexcept {
  auto &self = *static_cast<VrtRadioAdapter *>(context);
  if (window.count() > 1024 ||
      window.format() != vita::profiles::iq::SampleFormat::iq16)
    return std::unexpected(vita::Error{vita::ErrorCode::unsupported_capability});
  std::array<std::int16_t, 2048> samples{};
  void *buffers[]{samples.data()};
  int flags = 0;
  long long time_ns = 0;
  const auto count = self.device_->readStream(self.stream_, buffers,
                                               window.count(), flags, time_ns, 0);
  if (count != static_cast<int>(window.count()))
    return std::unexpected(vita::Error{vita::ErrorCode::invalid_state});
  for (std::size_t index = 0; index < window.count(); ++index) {
    auto written = window.write(index, samples[index * 2] / 32768.0,
                                samples[index * 2 + 1] / 32768.0);
    if (!written)
      return written;
  }
  return window.validate_complete();
}
} // namespace sdr