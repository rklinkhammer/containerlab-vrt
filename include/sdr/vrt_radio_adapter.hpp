#pragma once

#include <vita/runtime/public/config.hpp>

#include <memory>

#include <sdr/soapy_virtual_device.hpp>

namespace sdr {
class VrtRadioAdapter : public std::enable_shared_from_this<VrtRadioAdapter> {
public:
  static std::shared_ptr<VrtRadioAdapter>
  create(std::shared_ptr<SoapyVirtualDevice> device);
  ~VrtRadioAdapter();

  vita::DeviceBackendBinding device_binding();
  vita::profiles::iq::SourceProvider source_provider() noexcept;
  std::shared_ptr<SoapyVirtualDevice> device() const noexcept { return device_; }

private:
  explicit VrtRadioAdapter(std::shared_ptr<SoapyVirtualDevice> device);

  static vita::runtime::transaction::Validation
  validate(void *context, vita::FieldId id, vita::SemanticValue value,
           const vita::runtime::StateSnapshot &state) noexcept;
  static vita::Result<void>
  begin(void *context, const vita::runtime::PlannedField &field,
        vita::runtime::timing::Boundary boundary,
        vita::runtime::transaction::AsyncResult result) noexcept;
  static vita::runtime::transaction::BackendQuiescence
  quiescence(void *context) noexcept;
  static vita::runtime::transaction::BatchOutcome
  commit(void *context, const vita::runtime::ExecutionPlan &plan,
         vita::runtime::timing::Boundary boundary) noexcept;
  static vita::Result<void>
  produce(void *context, vita::profiles::iq::SampleWriteWindow &window) noexcept;

  std::shared_ptr<SoapyVirtualDevice> device_;
  SoapySDR::Stream *stream_{};
};
} // namespace sdr