#include <cassert>
#include <cstdlib>
#include <iostream>
#include <sdr/telemetry.hpp>
#include <sstream>
#include <thread>
#include <vector>
int main() {
  setenv("VRT_TELEMETRY_INTERVAL_MS", "100", 1);
  std::ostringstream output;
  sdr::telemetry::Reporter r("detector", output, "quote\"\ninstance");
  r.heartbeat({{"packets", 0}, {"queue_depth", nullptr}});
  r.activity();
  r.heartbeat({{"packets", 1}}, 1, true);
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i)
    threads.emplace_back([&] {
      for (int j = 0; j < 20; ++j)
        r.detection({{"frequency_hz", 1000}});
    });
  for (auto &t : threads)
    t.join();
  r.heartbeat({{"packets", 1}}, 1, true);
  std::istringstream in(output.str());
  std::string line;
  unsigned count = 0, det = 0;
  std::uint64_t seq = 0;
  while (std::getline(in, line)) {
    assert(line.size() + 1 <= 4096);
    auto j = nlohmann::json::parse(line);
    assert(j["schema"] == "vrt.telemetry/1");
    assert(j["sequence"].get<std::uint64_t>() > seq);
    seq = j["sequence"];
    assert(j["boot_id"].get<std::string>().size() == 32);
    if (count == 0) {
      assert(j["state"] == "idle");
      assert(j["last_successful_activity"].is_null());
    }
    if (j["event"] == "detection")
      ++det;
    if (++count == 13)
      assert(j["suppressed_events"] == 70);
  }
  assert(count == 13 && det == 10);
  setenv("VRT_TELEMETRY_INTERVAL_MS", "0", 1);
  std::ostringstream off;
  sdr::telemetry::Reporter disabled("radio", off);
  disabled.heartbeat({});
  disabled.detection({});
  disabled.failed();
  assert(off.str().empty());
  setenv("VRT_TELEMETRY_INTERVAL_MS", "100", 1);
  std::ostringstream bounded;
  sdr::telemetry::Reporter big("radio", bounded);
  big.detection({{"invalid", std::string(10000, 'x')}});
  big.heartbeat({});
  auto j = nlohmann::json::parse(bounded.str());
  assert(j["suppressed_events"] == 1);
  std::cout << "telemetry contract, concurrency, rate and size bounds PASS\n";
}
