#include "udp_pipeline.hpp"

#include <sdr/processor_controller.hpp>

#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void handle_stop(int) { stop_requested = 1; }
bool stopping() noexcept { return stop_requested != 0; }
} // namespace

int main(int argc, char **argv) {
  try {
    std::signal(SIGINT, handle_stop);
    std::signal(SIGTERM, handle_stop);
    std::string config_path = "generated/processor.json";
    const auto duration = sdr::udp::parse_duration(argc, argv, config_path);
    const auto config = sdr::udp::load_processor_config(config_path);
    std::unique_ptr<sdr::ProcessorController> controller;
    if (config.controller)
      controller = std::make_unique<sdr::ProcessorController>(*config.controller,
                                                              std::cout);
    const auto result =
      sdr::udp::run_processor(config, duration, std::cout, stopping);
    if (controller) {
      const auto metrics = controller->metrics();
      std::cout << nlohmann::json{
                       {"type", "controller_metrics"},
                       {"coordinated", controller->coordinated()},
                       {"connection_attempts", metrics.connection_attempts},
                       {"connections", metrics.connections},
                       {"reconnects", metrics.reconnects},
                       {"status_failures", metrics.status_failures},
                       {"protocol_failures", metrics.protocol_failures},
                       {"configurations", metrics.configurations},
                       {"starts_submitted", metrics.starts_submitted},
                       {"starts_admitted", metrics.starts_admitted},
                       {"stale_starts_replayed", metrics.stale_starts_replayed},
                       {"boot_changes", metrics.boot_changes}}
                       .dump()
                << '\n';
    }
    return result;
  } catch (const std::exception &error) {
    std::cerr << "processor: " << error.what() << '\n';
    return 2;
  }
}