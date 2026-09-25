#include <sdr/vrt_radio_adapter.hpp>
#include <vita/profiles/iq/lab.hpp>
#include <vita/runtime/public/runtime.hpp>
#include <cassert>
#include <memory>

// Independent clock scenarios: PPS remapping must preserve an admitted absolute
// start epoch, but must not excuse execution outside its timing window.
void scenario(bool late) {
  auto config = vita::profiles::iq::lab::config(0xabcdef);
  auto pools = vita::profiles::iq::lab::pools();
  assert(config && pools);
  config->clock.epoch = vita::runtime::timing::Epoch::utc;
  auto runtime = vita::VitaRuntime<1>::create(*config, std::move(*pools));
  assert(runtime);
  auto device = std::make_shared<sdr::SoapyVirtualDevice>("clock-test", 100'050'000, 0.25, 0.0);
  auto adapter = sdr::VrtRadioAdapter::create(device);
  vita::StreamConfig stream;
  stream.sid = 1; stream.controller_id = 1; stream.controllee_id = 2;
  stream.profile = vita::profiles::iq::Profile::sdr_radio;
  stream.sample_rate = 1'000'000;
  stream.trailer = true;
  stream.source = adapter->source_provider();
  stream.device = adapter->device_binding();
  auto controllee = (*runtime)->add_controllee(stream);
  assert(controllee);
  auto controller = (*runtime)->add_controller(*controllee);
  assert(controller);
  assert((*runtime)->observe_pps({0}, {1000, 0}));
  assert((*runtime)->progress({0}));
  vita::CommandOptions options;
  options.timeout_ns = 10'000'000'000ULL;
  auto configured = controller->configure(vita::SdrRadioSettings{});
  assert(configured);
  assert((*runtime)->run_for(10'000'000));
  auto configuration = controller->wait(*configured, 0, vita::WaitEvidence::execution);
  assert(configuration && configuration->observation.confirms_execution);
  auto start = controller->start({1005, 0}, options);
  assert(start);
  for (unsigned tick = 0; tick < 10; ++tick)
    assert((*runtime)->progress({10'000'000ULL + tick * 1'000'000ULL}));
  auto admission = controller->wait(*start, 0, vita::WaitEvidence::validation);
  assert(admission && admission->observation.validation_accepted);
  for (unsigned second = 1; second <= 4; ++second) {
    assert((*runtime)->observe_pps({second * 1'000'000'000ULL}, {1000 + second, 0}));
    assert((*runtime)->progress({second * 1'000'000'000ULL}));
    assert(!device->streaming());
  }
  assert((*runtime)->observe_pps({5'000'000'000ULL}, {1005, 0}));
  const auto tick = late ? 5'010'000'000ULL : 5'000'000'000ULL;
  for (int i = 0; i < 4; ++i) assert((*runtime)->progress({tick}));
  assert(device->streaming() == !late);
}
int main() { scenario(false); scenario(true); }
