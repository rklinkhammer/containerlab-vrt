#pragma once
#include <chrono>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <ostream>
#include <string>
namespace sdr::telemetry {
void write_line(std::ostream &, const std::string &);
class Reporter {
public:
  Reporter(std::string role, std::ostream &output, std::string instance = "");
  bool due() const;
  void activity();
  void heartbeat(const nlohmann::json &counters, std::uint64_t errors = 0,
                 bool final = false);
  void detection(const nlohmann::json &fields);
  void failed();

private:
  void emit(const char *, const char *, bool, const nlohmann::json &);
  std::string role_, instance_;
  std::ostream &output_;
  std::chrono::milliseconds interval_;
  std::chrono::steady_clock::time_point started_, next_, activity_{}, window_;
  std::chrono::system_clock::time_point activity_wall_{};
  std::uint64_t suppressed_{}, errors_{};
  unsigned events_{};
  mutable std::mutex mutex_;
};
} // namespace sdr::telemetry
