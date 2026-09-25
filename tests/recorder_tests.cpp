#include <sdr/recorder.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace {
sdr::recorder::Options parse(std::initializer_list<const char *> arguments) {
  std::vector<char *> values;
  for (const auto *argument : arguments)
    values.push_back(const_cast<char *>(argument));
  return sdr::recorder::parse_options(static_cast<int>(values.size()),
                                      values.data());
}

template <typename Function> void rejected(Function function) {
  bool threw = false;
  try {
    function();
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);
}

void parser_contract() {
  const auto generated = parse(
      {"recorder", "--interface", "eth1", "--metrics", "/tmp/metrics.json"});
  assert(generated.interface == "eth1");
  assert(generated.metrics_path == "/tmp/metrics.json");
  assert(!generated.pcap_path);
  assert(!generated.duration);
  assert(generated.capture_bytes == 64ULL * 1024 * 1024);

  const auto capture =
      parse({"recorder", "--interface", "eth1", "--metrics", "metrics.json",
             "--duration-seconds", "5", "--pcap", "capture.pcap",
             "--capture-bytes", "4096"});
  assert(capture.duration == std::chrono::seconds(5));
  assert(capture.pcap_path == std::filesystem::path("capture.pcap"));
  assert(capture.capture_bytes == 4096);

  rejected([] { parse({"recorder", "--interface", "eth1"}); });
  rejected([] {
    parse({"recorder", "--interface", "eth1", "--metrics", "m",
           "--duration-seconds", "61"});
  });
  rejected([] {
    parse({"recorder", "--interface", "eth1", "--metrics", "m",
           "--duration-seconds", "5"});
  });
  rejected([] {
    parse({"recorder", "--interface", "eth1", "--metrics", "m",
           "--capture-bytes", "4096"});
  });
  rejected([] {
    parse({"recorder", "--interface", "eth1", "--metrics", "m", "--pcap",
           "p", "--capture-bytes", "67108865"});
  });
}

void metrics_contract() {
  auto options = parse(
      {"recorder", "--interface", "eth1", "--metrics", "metrics.json"});
  sdr::recorder::Metrics metrics;
  metrics.observe_frame(100, 100);
  metrics.observe_frame(70'000, sdr::recorder::receive_buffer_bytes);
  metrics.observe_kernel(3, 2);
  const auto json = sdr::recorder::metrics_json(
      options, metrics, std::chrono::milliseconds(250), 1234, "running");
  assert(json.at("schema") == 1 && json.at("role") == "recorder");
  assert(json.at("passive") == true && json.at("status") == "running");
  assert(json.at("receive").at("frames") == 2);
  assert(json.at("receive").at("wire_bytes") == 70'100);
  assert(json.at("receive").at("truncations") == 1);
  assert(json.at("receive").at("kernel_packets") == 3);
  assert(json.at("receive").at("kernel_drops") == 2);
  assert(json.at("limits").at("capture_bytes").is_null());
}

void pcap_bound_contract() {
  const auto path = std::filesystem::temp_directory_path() /
                    "containerlab-vrt-recorder-test.pcap";
  std::filesystem::remove(path);
  {
    sdr::recorder::PcapWriter writer(path, 44);
    const std::array<std::byte, 4> frame{};
    assert(writer.write(frame, frame.size(),
                        std::chrono::system_clock::time_point{}));
    assert(writer.bytes() == 44);
    assert(!writer.write(frame, frame.size(),
                         std::chrono::system_clock::time_point{}));
    assert(writer.limit_reached());
    writer.finish();
  }
  assert(std::filesystem::file_size(path) == 44);
  std::filesystem::remove(path);
}
} // namespace

int main() {
  parser_contract();
  metrics_contract();
  pcap_bound_contract();
}