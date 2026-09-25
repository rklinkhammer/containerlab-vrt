#include <sdr/telemetry.hpp>
#include "udp_pipeline.hpp"

#include <csignal>
#include <exception>
#include <iostream>

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void handle_stop(int) { stop_requested = 1; }
bool stopping() noexcept { return stop_requested != 0; }
} // namespace

int main(int argc, char **argv) {
  try {
    std::signal(SIGINT, handle_stop);
    std::signal(SIGTERM, handle_stop);
    std::string config_path = "generated/detector.json";
    const auto duration = sdr::udp::parse_duration(argc, argv, config_path);
    return sdr::udp::run_detector(sdr::udp::load_detector_config(config_path),
                                  duration, std::cout, stopping);
  } catch (const std::exception &) {
    try { sdr::telemetry::Reporter("detector",std::cout).failed(); } catch (...) {}
    std::cerr << "detector: application error (see documented configuration requirements)\n";
    return 2;
  }
}