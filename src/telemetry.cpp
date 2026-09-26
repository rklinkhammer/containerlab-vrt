#include <atomic>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <random>
#include <sdr/telemetry.hpp>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
namespace sdr::telemetry {
namespace {
std::mutex output_mutex;
std::atomic<std::uint64_t> sequence{};
const auto process_start = std::chrono::steady_clock::now();
std::string utc(std::chrono::system_clock::time_point now =
                    std::chrono::system_clock::now()) {
  auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&t, &tm);
  std::ostringstream s;
  s << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
    << std::setfill('0')
    << (std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch())
            .count() %
        1000)
    << 'Z';
  return s.str();
}
const std::string &boot() {
  static const std::string id = []() {
    std::random_device r;
    std::string s(32, '0');
    for (auto &c : s)
      c = "0123456789abcdef"[r() & 15];
    return s;
  }();
  return id;
}
std::chrono::milliseconds interval() {
  const char *s = std::getenv("VRT_TELEMETRY_INTERVAL_MS");
  if (!s)
    return std::chrono::milliseconds(5000);
  std::string text(s);
  std::size_t end{};
  auto n = std::stoul(text, &end);
  if (end != text.size() || (n != 0 && (n < 100 || n > 60000)))
    throw std::invalid_argument("invalid telemetry interval");
  return std::chrono::milliseconds(n);
}
} // namespace
HealthAssessment ActivityHealth::assess(Clock::time_point now,
    std::optional<Clock::time_point> activity, std::uint64_t errors,
    std::chrono::milliseconds interval, bool final) {
  const bool reset=previous_ && errors<errors_;
  HealthAssessment result{"unknown","recovery_unproven",std::nullopt,reset,std::nullopt};
  if(previous_)result.sample_window_ms=std::chrono::duration_cast<std::chrono::milliseconds>(now-*previous_).count();
  if(!reset)result.new_errors=errors-errors_;
  if(reset || result.new_errors.value_or(0)>0)recovery_since_=now;
  errors_=errors;
  previous_=now;
  if(failed_) { result.state="failed";result.reason="fatal_error"; }
  else if(final) { result.reason="shutdown"; }
  else if(reset) { result.reason="counter_reset"; }
  else if(result.new_errors.value_or(0)>0) { result.state="degraded";result.reason="new_errors"; }
  else if(activity && *activity>now) { result.reason="invalid_activity_time"; }
  else if(!activity || now-*activity>interval*2) { result.state="idle";result.reason="no_recent_activity"; }
  else if(!recovery_since_ || (*activity>*recovery_since_ && now-*recovery_since_>=interval)) {
    result.state="healthy";result.reason="recent_activity";recovery_since_.reset();
  }
  return result;
}
void write_line(std::ostream &out, const std::string &line) {
  std::lock_guard lock(output_mutex);
  out << line << '\n' << std::flush;
}
Reporter::Reporter(std::string role, std::ostream &out, std::string instance)
    : role_(std::move(role)), instance_(std::move(instance)), output_(out),
      interval_(interval()), started_(process_start), next_(started_),
      window_(started_) {
  if (const char *id = std::getenv("VRT_INSTANCE_ID"))
    instance_ = id;
  if (instance_.empty()) {
    char h[128]{};
    if (gethostname(h, sizeof(h) - 1) == 0)
      instance_ = h;
    else
      instance_ = role_;
  }
  instance_.resize(std::min<std::size_t>(instance_.size(), 128));
}
bool Reporter::due() const {
  std::lock_guard lock(mutex_);
  return interval_.count() != 0 && std::chrono::steady_clock::now() >= next_;
}
void Reporter::activity() {
  if (interval_.count() == 0)
    return;
  std::lock_guard lock(mutex_);
  activity_ = std::chrono::steady_clock::now();
  activity_wall_ = std::chrono::system_clock::now();
}
void Reporter::emit(const char *event, const char *state, bool ready,
                    const nlohmann::json &fields) {
  nlohmann::json j = {
      {"schema", "vrt.telemetry/1"},
      {"health_policy", "local-activity/2"},
      {"event", event},
      {"timestamp", utc()},
      {"role", role_},
      {"instance", instance_},
      {"boot_id", boot()},
      {"sequence", ++sequence},
      {"uptime_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started_)
                        .count()},
      {"state", state},
      {"ready", ready},
      {"last_successful_activity",
       activity_ == std::chrono::steady_clock::time_point{}
           ? nlohmann::json(nullptr)
           : nlohmann::json(utc(activity_wall_))},
      {"suppressed_events", suppressed_},
      {"data", fields}};
  auto line = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
  if (line.size() + 1 > 4096) {
    ++suppressed_;
    return;
  }
  write_line(output_, line);
}
void Reporter::heartbeat(const nlohmann::json &counters, std::uint64_t errors,
                         bool final) {
  std::lock_guard lock(mutex_);
  if (interval_.count() == 0)
    return;
  auto now = std::chrono::steady_clock::now();
  if (!final && now < next_)
    return;
  const auto assessment=health_.assess(now,
      activity_==std::chrono::steady_clock::time_point{} ? std::nullopt : std::optional{activity_},
      errors,interval_,final);
  auto fields=counters;
  fields["health"]={{"scope","local_activity"},{"reason",assessment.reason},
    {"error_total",errors},
    {"new_errors",assessment.new_errors ? nlohmann::json(*assessment.new_errors) : nlohmann::json(nullptr)},
    {"counter_reset",assessment.counter_reset},
    {"sample_window_ms",assessment.sample_window_ms ? nlohmann::json(*assessment.sample_window_ms) : nlohmann::json(nullptr)},
    {"activity_window_ms",interval_.count()*2}};
  emit(final ? "shutdown" : "heartbeat",assessment.state,!final && !health_.failed(),fields);
  next_ = now + interval_;
}
void Reporter::detection(const nlohmann::json &fields) {
  std::lock_guard lock(mutex_);
  if (interval_.count() == 0)
    return;
  auto now = std::chrono::steady_clock::now();
  if (now - window_ >= std::chrono::seconds(1)) {
    events_ = 0;
    window_ = now;
  }
  if (events_++ >= 10) {
    ++suppressed_;
    return;
  }
  emit("detection", "unknown", true, fields);
}
void Reporter::failed() {
  std::lock_guard lock(mutex_);
  health_.fail();
  if (interval_.count() != 0)
    emit("failure", "failed", false, {{"reason", "APPLICATION_ERROR"}});
}
} // namespace sdr::telemetry
