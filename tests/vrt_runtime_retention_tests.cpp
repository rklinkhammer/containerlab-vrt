#include <vita/profiles/iq/lab.hpp>
#include <vita/runtime/public/runtime.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>

int main() {
  auto config = vita::profiles::iq::lab::config(0xabcdef);
  auto pools = vita::profiles::iq::lab::pools();
  assert(config && pools);
  config->clock.epoch = vita::runtime::timing::Epoch::utc;
  auto runtime = vita::VitaRuntime<1, 2, 16, 32768>::create(
      *config, std::move(*pools));
  assert(runtime);

  vita::StreamConfig stream;
  stream.sid = 1;
  stream.controller_id = 1;
  stream.controllee_id = 2;
  stream.profile = vita::profiles::iq::Profile::frequency_tunable;
  stream.sample_rate = 1'000'000;
  auto controllee = (*runtime)->add_controllee(stream);
  assert(controllee);
  auto controller = (*runtime)->add_controller(*controllee);
  assert(controller);
  assert((*runtime)->observe_pps({0}, {1000, 0}));

  std::uint64_t now = 0;
  for (std::size_t iteration = 0; iteration < 300; ++iteration) {
    vita::CommandOptions options;
    options.timeout_ns = 1;
    auto query = controller->query(1u << 1, options);
    assert(query);
    now += 2;
    assert((*runtime)->progress({now}));
    assert(controller->release(*query));
    now += 30'000'000'001ULL;
    assert((*runtime)->progress({now}));
  }
}