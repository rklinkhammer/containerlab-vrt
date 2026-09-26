#pragma once
#include <chrono>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <ostream>
#include <optional>
#include <string>
namespace sdr::telemetry {
void write_line(std::ostream &, const std::string &);
struct HealthAssessment {
  const char *state;
  const char *reason;
  std::optional<std::uint64_t> new_errors;
  bool counter_reset;
  std::optional<std::int64_t> sample_window_ms;
};
// Serialized policy; monotonic time is supplied explicitly for independent tests.
class ActivityHealth {
public:
  using Clock = std::chrono::steady_clock;
  HealthAssessment assess(Clock::time_point now,
                          std::optional<Clock::time_point> activity,
                          std::uint64_t error_total,
                          std::chrono::milliseconds interval, bool final=false);
  void fail() noexcept { failed_=true; }
  bool failed() const noexcept { return failed_; }
private:
  std::uint64_t errors_{};
  std::optional<Clock::time_point> previous_, recovery_since_;
  bool failed_{};
};
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
  std::uint64_t suppressed_{};
  ActivityHealth health_;
  unsigned events_{};
  mutable std::mutex mutex_;
};
} // namespace sdr::telemetry
