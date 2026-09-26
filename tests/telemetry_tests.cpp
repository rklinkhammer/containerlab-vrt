#include <cassert>
#include <cstdlib>
#include <iostream>
#include <sdr/telemetry.hpp>
#include <sstream>
#include <thread>
#include <vector>
int main() {
  {
    using namespace std::chrono;
    using Policy=sdr::telemetry::ActivityHealth;
    const auto origin=Policy::Clock::time_point{};
    auto at=[&](int ms){return origin+milliseconds(ms);};
    Policy policy;
    auto result=policy.assess(at(0),std::nullopt,0,milliseconds(100));
    assert(std::string(result.state)=="idle" && result.new_errors==0 && !result.sample_window_ms);
    result=policy.assess(at(100),at(90),0,milliseconds(100));
    assert(std::string(result.state)=="healthy");
    result=policy.assess(at(200),at(190),3,milliseconds(100));
    assert(std::string(result.state)=="degraded" && result.new_errors==3);
    result=policy.assess(at(300),at(190),3,milliseconds(100));
    assert(std::string(result.state)=="unknown"); // heartbeat alone cannot recover
    result=policy.assess(at(400),at(390),3,milliseconds(100));
    assert(std::string(result.state)=="healthy" && result.new_errors==0);
    result=policy.assess(at(700),at(390),3,milliseconds(100));
    assert(std::string(result.state)=="idle");
    result=policy.assess(at(800),at(790),1,milliseconds(100));
    assert(std::string(result.state)=="unknown" && result.counter_reset && !result.new_errors);
    result=policy.assess(at(900),at(790),1,milliseconds(100));
    assert(std::string(result.state)=="unknown");
    result=policy.assess(at(1000),at(990),1,milliseconds(100));
    assert(std::string(result.state)=="healthy");
    result=policy.assess(at(1100),at(1090),1,milliseconds(100),true);
    assert(std::string(result.state)=="unknown" && std::string(result.reason)=="shutdown");
    policy.fail();
    result=policy.assess(at(1200),at(1190),1,milliseconds(100));
    assert(std::string(result.state)=="failed");
    result=policy.assess(at(1300),at(1290),1,milliseconds(100),true);
    assert(std::string(result.state)=="failed");
    Policy fresh;
    assert(fresh.assess(at(1400),std::nullopt,0,milliseconds(100)).new_errors==0);
  }

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
    assert(j["health_policy"] == "local-activity/2");
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
  std::ostringstream terminal_output;
  sdr::telemetry::Reporter terminal("processor",terminal_output);
  terminal.activity();terminal.failed();terminal.heartbeat({},0,true);
  std::istringstream terminal_lines(terminal_output.str());
  unsigned terminal_count=0;
  while(std::getline(terminal_lines,line)) {
    auto event=nlohmann::json::parse(line);
    assert(event["state"]=="failed" && event["ready"]==false);
    ++terminal_count;
  }
  assert(terminal_count==2);
  std::cout << "telemetry contract, concurrency, rate and size bounds PASS\n";
}
