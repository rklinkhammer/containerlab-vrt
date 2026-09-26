#include <vita/profiles/iq/lab.hpp>
#include <vita/runtime/public/runtime.hpp>
#include <cassert>
#include <sdr/vrt_radio_adapter.hpp>
#include <sdr/soapy_virtual_device.hpp>
#include <cstdint>

int main() {
  auto config=vita::profiles::iq::lab::config(vita::profiles::iq::sdr_unknown_oui);
  auto pools=vita::profiles::iq::lab::pools();
  assert(config && pools);
  config->clock.epoch=vita::runtime::timing::Epoch::utc;
  auto runtime=vita::VitaRuntime<1,2,16,32768>::create(*config,std::move(*pools));
  assert(runtime);
  vita::StreamConfig stream;
  stream.sid=1;stream.controller_id=1;stream.controllee_id=2;
  stream.profile=vita::profiles::iq::Profile::sdr_radio;
  stream.sample_rate=1'000'000;
  auto device=std::make_shared<sdr::SoapyVirtualDevice>("resumption",100050000,0.25,0.0);
  auto adapter=sdr::VrtRadioAdapter::create(device);
  stream.trailer=true;stream.source=adapter->source_provider();stream.device=adapter->device_binding();
  auto radio=(*runtime)->add_controllee(stream);assert(radio);
  auto controller=(*runtime)->add_controller(*radio);assert(controller);
  assert((*runtime)->observe_pps({0},{1000,0}));
  std::uint64_t tick=0;
  auto query=[&](std::uint32_t expected) {
    auto handle=controller->status();assert(handle);
    for(int i=0;i<10;++i)assert((*runtime)->progress({++tick}));
    assert(radio->admitted_message_id()==expected);
    assert(controller->release(*handle));
  };
  assert(controller->resume_commands_after(100));query(101);
  assert(controller->resume_commands_after(0));query(102); // cannot rewind
  auto exhausted=controller->resume_commands_after(UINT32_MAX);
  assert(!exhausted && exhausted.error().code==vita::ErrorCode::resource_limit);
  query(103); // rejected floor leaves current state unchanged
  vita::runtime::transaction::ControllerRegistry<> registry;
  assert(!registry.resume_after({999},1));
  assert(!registry.resume_after({0},1)); // unused relationship
}
