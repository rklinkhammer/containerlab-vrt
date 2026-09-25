#include <sdr/processor_controller.hpp>

#include <vita/codec/packet.hpp>
#include <vita/profiles/iq/lab.hpp>
#include <vita/runtime/public/runtime.hpp>
#include <vita/runtime/transport/framing.hpp>
#include <vita/runtime/transport/stream_framer.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sdr {
namespace {
using Clock = std::chrono::steady_clock;
using Runtime = vita::VitaRuntime<1, 16, 256, 1024 * 1024>;
using vita::runtime::transport::Association;
using vita::runtime::transport::RejectedSubmission;
using vita::runtime::transport::TxSubmission;
using vita::runtime::transport::TxToken;

struct Descriptor {
  int value{-1};
  Descriptor() = default;
  explicit Descriptor(int descriptor) : value(descriptor) {}
  Descriptor(const Descriptor &) = delete;
  Descriptor &operator=(const Descriptor &) = delete;
  Descriptor(Descriptor &&other) noexcept
      : value(std::exchange(other.value, -1)) {}
  Descriptor &operator=(Descriptor &&other) noexcept {
    if (this != &other) {
      reset();
      value = std::exchange(other.value, -1);
    }
    return *this;
  }
  ~Descriptor() { reset(); }
  void reset(int replacement = -1) noexcept {
    if (value >= 0)
      close(value);
    value = replacement;
  }
};

bool set_nonblocking(int descriptor) noexcept {
  const auto flags = fcntl(descriptor, F_GETFL, 0);
  return flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

struct StatusSnapshot {
  std::string boot_id;
  std::optional<std::uint64_t> generation;
  bool ready{};
  bool active{};
  bool streaming{};
};

bool wait_socket(int descriptor, short events, Clock::time_point deadline) {
  while (Clock::now() < deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - Clock::now());
    pollfd item{descriptor, events, 0};
    const auto result = poll(&item, 1, std::max(1, static_cast<int>(remaining.count())));
    if (result > 0)
      return (item.revents & events) != 0;
    if (result < 0 && errno != EINTR)
      return false;
  }
  return false;
}

Descriptor connect_blocking(const std::string &host, std::uint16_t port,
                            std::chrono::milliseconds timeout) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *raw = nullptr;
  const auto service = std::to_string(port);
  if (getaddrinfo(host.c_str(), service.c_str(), &hints, &raw) != 0)
    return {};
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(raw, freeaddrinfo);
  Descriptor socket_value(socket(raw->ai_family, raw->ai_socktype, raw->ai_protocol));
  if (socket_value.value < 0 || !set_nonblocking(socket_value.value))
    return {};
  const auto connected = connect(socket_value.value, raw->ai_addr, raw->ai_addrlen);
  if (connected < 0 && errno != EINPROGRESS)
    return {};
  if (connected < 0) {
    const auto deadline = Clock::now() + timeout;
    if (!wait_socket(socket_value.value, POLLOUT, deadline))
      return {};
    int error = 0;
    socklen_t length = sizeof(error);
    if (getsockopt(socket_value.value, SOL_SOCKET, SO_ERROR, &error, &length) < 0 ||
        error != 0)
      return {};
  }
  return socket_value;
}

bool write_all(int descriptor, std::span<const std::byte> bytes,
               Clock::time_point deadline) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    if (!wait_socket(descriptor, POLLOUT, deadline))
      return false;
    const auto count = send(descriptor, bytes.data() + offset,
                            bytes.size() - offset, 0);
    if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
      continue;
    if (count <= 0)
      return false;
    offset += static_cast<std::size_t>(count);
  }
  return true;
}

std::optional<StatusSnapshot>
read_status(const ProcessorRadioControlConfig &config,
            std::chrono::milliseconds timeout) {
  auto connection = connect_blocking(config.status_host, config.status_port, timeout);
  if (connection.value < 0)
    return std::nullopt;
  const std::string payload = R"({"version":1,"operation":"get_status"})";
  std::vector<std::byte> request(payload.size() + 4);
  for (std::size_t index = 0; index < 4; ++index)
    request[index] = std::byte((payload.size() >> (8 * (3 - index))) & 0xff);
  std::memcpy(request.data() + 4, payload.data(), payload.size());
  const auto deadline = Clock::now() + timeout;
  if (!write_all(connection.value, request, deadline))
    return std::nullopt;
  std::array<std::byte, 4> header{};
  std::size_t received = 0;
  while (received < header.size()) {
    if (!wait_socket(connection.value, POLLIN, deadline))
      return std::nullopt;
    const auto count = recv(connection.value, header.data() + received,
                            header.size() - received, 0);
    if (count <= 0)
      return std::nullopt;
    received += static_cast<std::size_t>(count);
  }
  std::size_t size = 0;
  for (const auto value : header)
    size = (size << 8) | std::to_integer<unsigned>(value);
  if (size == 0 || size > 4096)
    return std::nullopt;
  std::array<char, 4096> response{};
  received = 0;
  while (received < size) {
    if (!wait_socket(connection.value, POLLIN, deadline))
      return std::nullopt;
    const auto count = recv(connection.value, response.data() + received,
                            size - received, 0);
    if (count <= 0)
      return std::nullopt;
    received += static_cast<std::size_t>(count);
  }
  try {
    const auto value = nlohmann::json::parse(response.data(), response.data() + size);
    if (value.at("version") != 1 || value.at("radio_id") != config.id)
      return std::nullopt;
    StatusSnapshot result;
    result.boot_id = value.at("boot_id").get<std::string>();
    result.ready = value.at("ready").get<bool>();
    result.active = value.at("connection").at("active").get<bool>();
    result.streaming = value.at("streaming").get<bool>();
    if (!value.at("connection").at("generation").is_null())
      result.generation = value.at("connection").at("generation").get<std::uint64_t>();
    if (result.boot_id.empty())
      return std::nullopt;
    return result;
  } catch (...) {
    return std::nullopt;
  }
}

class ClientTransport {
public:
  struct Setup {
    ProcessorRadioControlConfig config;
    std::size_t queue_limit{};
    std::chrono::milliseconds io_timeout{};
    std::shared_ptr<ClientTransport> instance;
  };

  static vita::runtime::transport::TransportFactory factory(Setup &setup) noexcept;
  ~ClientTransport() { detach(); }
  bool connected() const noexcept { return socket_.value >= 0; }
  std::uint64_t generation() const noexcept { return generation_; }
  std::uint64_t received_packets() const noexcept { return received_packets_; }
  std::uint64_t route_failures() const noexcept { return route_failures_; }
  void request_reconnect() noexcept {
    if (socket_.value >= 0)
      fail();
    else
      reconnect_requested_ = true;
  }
  void detach() noexcept;

private:
  struct Slot {
    std::optional<TxSubmission> submission;
    vita::runtime::AdmissionBundle credits;
    std::size_t segment{};
    std::size_t offset{};
    std::uint64_t generation{1};
    Clock::time_point deadline{};
  };

  ClientTransport(Setup value, vita::runtime::transport::HostBindings host)
      : setup_(std::move(value)), admission_(&host.admission),
        counters_(&host.counters),
        ingress_(host.routes, std::move(host.rx_data), std::move(host.rx_control),
                 std::move(host.rx_cancellation), {}, 2048) {}
  void fail() noexcept;
  vita::Result<bool> progress() noexcept;
  vita::Result<bool> receive() noexcept;
  vita::Result<bool> transmit(Slot &slot) noexcept;

  Setup setup_;
  Descriptor socket_;
  vita::runtime::AdmissionPool *admission_{};
  vita::runtime::CounterRegistry<128> *counters_{};
  vita::runtime::transport::StreamIngress<2048, 128> ingress_;
  std::array<Slot, 64> slots_{};
  std::optional<vita::runtime::PeerSession> local_;
  std::optional<vita::runtime::PeerSession> remote_;
  Clock::time_point retry_at_{};
  std::uint64_t generation_{};
  std::uint64_t received_packets_{};
  std::uint64_t route_failures_{};
  std::size_t cursor_{};
  bool open_{true};
  bool accepting_{true};
  bool reconnect_requested_{true};
};

void ClientTransport::fail() noexcept {
  socket_.reset();
  static_cast<void>(ingress_.disconnect());
  for (auto &slot : slots_)
    if (slot.submission) {
      vita::runtime::CompletionResult result;
      result.status = vita::runtime::CompletionStatus::failed;
      result.error = vita::Error{vita::ErrorCode::invalid_state};
      slot.submission->completion.publish(result);
      slot.submission.reset();
      slot.credits.reset();
      ++slot.generation;
    }
  reconnect_requested_ = true;
  retry_at_ = Clock::now() + std::chrono::milliseconds(100);
}

vita::Result<bool> ClientTransport::receive() noexcept {
  std::array<std::byte, 2048> bytes{};
  const auto count = recv(socket_.value, bytes.data(), bytes.size(), 0);
  if (count == 0) {
    fail();
    return true;
  }
  if (count < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return false;
    fail();
    return true;
  }
  auto fed = ingress_.feed(vita::Bytes{bytes}.first(static_cast<std::size_t>(count)));
  if (!fed) {
    ++route_failures_;
    fail();
    return true;
  }
  received_packets_ += *fed;
  return true;
}

vita::Result<bool> ClientTransport::transmit(Slot &slot) noexcept {
  if (Clock::now() >= slot.deadline || socket_.value < 0) {
    fail();
    return true;
  }
  std::array<iovec, 3> vectors{};
  std::size_t count = 0;
  for (auto index = slot.segment;
       index < slot.submission->storage.segment_count(); ++index) {
    auto segment = slot.submission->storage.segment(index);
    if (!segment)
      return std::unexpected(segment.error());
    const auto offset = index == slot.segment ? slot.offset : 0;
    vectors[count++] = {const_cast<std::byte *>(segment->data() + offset),
                        segment->size() - offset};
  }
  msghdr message{};
  message.msg_iov = vectors.data();
  message.msg_iovlen = count;
#ifdef MSG_NOSIGNAL
  const auto sent = sendmsg(socket_.value, &message, MSG_NOSIGNAL);
#else
  const auto sent = sendmsg(socket_.value, &message, 0);
#endif
  if (sent < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return false;
    fail();
    return true;
  }
  std::size_t consumed = static_cast<std::size_t>(sent);
  while (consumed && slot.segment < slot.submission->storage.segment_count()) {
    auto segment = slot.submission->storage.segment(slot.segment);
    if (!segment)
      return std::unexpected(segment.error());
    const auto remaining = segment->size() - slot.offset;
    if (consumed < remaining) {
      slot.offset += consumed;
      consumed = 0;
    } else {
      consumed -= remaining;
      ++slot.segment;
      slot.offset = 0;
    }
  }
  if (slot.segment != slot.submission->storage.segment_count())
    return true;
  vita::runtime::CompletionResult result;
  result.status = vita::runtime::CompletionStatus::succeeded;
  result.value = slot.submission->storage.byte_size();
  slot.submission->completion.publish(result);
  slot.submission.reset();
  slot.credits.reset();
  ++slot.generation;
  return true;
}

vita::Result<bool> ClientTransport::progress() noexcept {
  if (!open_)
    return false;
  if (reconnect_requested_ && accepting_ && Clock::now() >= retry_at_ && remote_) {
    auto candidate = connect_blocking(setup_.config.control_host,
                                      setup_.config.control_port,
                                      setup_.io_timeout);
    if (candidate.value >= 0) {
      socket_ = std::move(candidate);
      reconnect_requested_ = false;
      ++generation_;
      auto reset = ingress_.reconnect(*remote_);
      if (!reset) {
        fail();
        return std::unexpected(reset.error());
      }
      return true;
    }
    retry_at_ = Clock::now() + std::chrono::milliseconds(100);
    return false;
  }
  if (socket_.value >= 0) {
    auto input = receive();
    if (!input || *input)
      return input;
  }
  for (std::size_t attempt = 0; attempt < slots_.size(); ++attempt) {
    auto &slot = slots_[cursor_++ % slots_.size()];
    if (slot.submission)
      return transmit(slot);
  }
  return false;
}

void ClientTransport::detach() noexcept {
  if (!open_)
    return;
  open_ = false;
  fail();
  ingress_.shutdown();
  admission_ = nullptr;
  counters_ = nullptr;
}

vita::runtime::transport::TransportFactory
ClientTransport::factory(Setup &setup) noexcept {
  vita::runtime::transport::Capabilities capabilities;
  capabilities.max_packet_bytes = 2048;
  capabilities.max_tx_segments = 3;
  capabilities.max_rx_fragments = 1;
  capabilities.reserved_control_slots = 32;
  capabilities.reserved_cancellation_slots = 16;
  vita::runtime::transport::TransportFactory result;
  result.context = &setup;
  result.required_bytes = sizeof(ClientTransport) + 128;
  result.slot_capacity = 64;
  result.capabilities = capabilities;
  result.create = [](void *context, vita::runtime::transport::HostBindings host)
      noexcept -> vita::Result<vita::runtime::transport::TransportBinding> {
    try {
      auto &setup = *static_cast<Setup *>(context);
      if (setup.config.control_host.empty() || !setup.config.control_port ||
          !setup.config.sid || setup.queue_limit == 0 || setup.queue_limit > 64 ||
          setup.io_timeout <= std::chrono::milliseconds::zero() ||
          setup.io_timeout > std::chrono::seconds(2))
        return std::unexpected(vita::Error{vita::ErrorCode::invalid_argument});
      auto owner = std::shared_ptr<ClientTransport>(
          new ClientTransport(setup, std::move(host)));
      vita::runtime::transport::TransportBinding binding;
      binding.owner = owner;
      binding.context = owner.get();
      binding.metadata_bytes = sizeof(ClientTransport) + 128;
      binding.slot_capacity = 64;
      binding.capabilities = ClientTransport::factory(setup).capabilities;
      binding.send = [](void *pointer, TxSubmission &&submission) noexcept
          -> std::expected<TxToken, RejectedSubmission> {
        auto &self = *static_cast<ClientTransport *>(pointer);
        auto reject = [&](vita::Error error) {
          return std::expected<TxToken, RejectedSubmission>{std::unexpected(
              RejectedSubmission{error, std::move(submission)})};
        };
        if (!self.open_ || self.socket_.value < 0 || !self.local_ ||
            submission.source != *self.local_ ||
            !submission.completion.is_reserved())
          return reject(vita::Error{vita::ErrorCode::invalid_state});
        auto framing = vita::runtime::transport::inspect(submission.storage);
        if (!framing || !vita::codec::is_command(framing->envelope.type) ||
            framing->packet_bytes > 2048)
          return reject(framing ? vita::Error{vita::ErrorCode::invalid_argument}
                                : framing.error());
        std::size_t selected = self.slots_.size();
        for (std::size_t index = 0; index < self.setup_.queue_limit; ++index)
          if (!self.slots_[index].submission) {
            selected = index;
            break;
          }
        if (selected == self.slots_.size())
          return reject(vita::Error{vita::ErrorCode::capacity_exhausted});
        vita::runtime::AdmissionRequest request;
        request.need(framing->envelope.cancel
                         ? vita::runtime::Resource::cancellation_queue
                         : vita::runtime::Resource::ordinary_queue);
        auto credits = self.admission_->acquire(request);
        if (!credits)
          return reject(credits.error());
        auto counted = self.counters_->accept(submission.counter,
                                              framing->envelope.packet_count);
        if (!counted)
          return reject(counted.error());
        auto &slot = self.slots_[selected];
        slot.submission.emplace(std::move(submission));
        slot.credits = std::move(*credits);
        slot.segment = slot.offset = 0;
        slot.deadline = Clock::now() + self.setup_.io_timeout;
        return TxToken{selected, slot.generation};
      };
      binding.progress = [](void *pointer) noexcept {
        return static_cast<ClientTransport *>(pointer)->progress();
      };
      binding.pending = [](void *pointer, TxToken token) noexcept {
        auto &self = *static_cast<ClientTransport *>(pointer);
        return token.slot < self.slots_.size() &&
               self.slots_[token.slot].generation == token.generation &&
               self.slots_[token.slot].submission.has_value();
      };
      binding.close_admission = [](void *pointer) noexcept {
        static_cast<ClientTransport *>(pointer)->accepting_ = false;
      };
      binding.associate = [](void *pointer, std::span<const Association> associations,
                             bool commit) noexcept -> vita::Result<void> {
        auto &self = *static_cast<ClientTransport *>(pointer);
        if (associations.size() != 1 ||
            associations[0].sid != self.setup_.config.sid ||
            associations[0].local.peer != 1 || associations[0].remote.peer != 2 ||
            !associations[0].local.generation ||
            associations[0].local.generation != associations[0].remote.generation)
          return std::unexpected(vita::Error{vita::ErrorCode::identity_conflict});
        if (commit) {
          self.local_ = associations[0].local;
          self.remote_ = associations[0].remote;
        }
        return {};
      };
      binding.detach = [](void *pointer) noexcept {
        static_cast<ClientTransport *>(pointer)->detach();
      };
      setup.instance = owner;
      return binding;
    } catch (...) {
      return std::unexpected(vita::Error{vita::ErrorCode::capacity_exhausted});
    }
  };
  return result;
}

vita::runtime::timing::ProtocolTime utc_now() {
  const auto elapsed = std::chrono::system_clock::now().time_since_epoch();
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
  return {static_cast<std::uint64_t>(seconds.count()),
          static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed - seconds)
                  .count()) *
              1000};
}

bool accepted(Runtime::Controller &controller, vita::TransactionHandle handle,
              vita::WaitEvidence evidence) {
  auto result = controller.wait(handle, 0, evidence);
  return result && result->status == vita::WaitStatus::evidence_received &&
         (evidence == vita::WaitEvidence::validation
              ? result->observation.validation_accepted
              : result->observation.confirms_execution);
}
} // namespace

struct ProcessorController::Implementation {
  struct Session {
    ClientTransport::Setup transport;
    std::unique_ptr<Runtime> runtime;
    std::optional<Runtime::Controller> controller;
    std::optional<StatusSnapshot> status;
    std::uint64_t reconciled_generation{};
    std::uint64_t last_clock_second{};
    Clock::time_point last_liveness{};
    std::size_t consecutive_failures{};
    bool configured{};
    bool started{};
  };

  ProcessorControllerConfig config;
  std::ostream *events;
  std::array<Session, 4> sessions;
  mutable std::mutex mutex;
  ProcessorControllerMetrics metrics_;
  std::atomic<bool> stop{false};
  std::atomic<bool> coordinated_{false};
  Clock::time_point origin{Clock::now()};
  vita::runtime::timing::ProtocolTime clock_origin{utc_now()};
  std::jthread worker;

  Implementation(ProcessorControllerConfig value, std::ostream &stream)
      : config(std::move(value)), events(&stream) {
    for (std::size_t index = 0; index < sessions.size(); ++index) {
      auto &session = sessions[index];
      session.transport = {config.radios[index], config.queue_limit,
                           config.io_timeout, {}};
      auto runtime_config = vita::profiles::iq::lab::config(
          vita::profiles::iq::sdr_unknown_oui);
      auto pools = vita::profiles::iq::lab::pools();
      if (!runtime_config || !pools)
        throw std::runtime_error("processor controller allocation failed");
      runtime_config->clock.epoch = vita::runtime::timing::Epoch::utc;
      runtime_config->transport = ClientTransport::factory(session.transport);
      auto made = Runtime::create(*runtime_config, std::move(*pools));
      if (!made)
        throw std::runtime_error("processor controller runtime setup failed");
      session.runtime = std::move(*made);
      if (!session.runtime->observe_pps({0}, clock_origin))
        throw std::runtime_error("processor controller clock setup failed");
      vita::RemoteTargetConfig target;
      target.sid = config.radios[index].sid;
      target.controller_id = 1;
      target.controllee_id = 2;
      target.profile = vita::profiles::iq::Profile::sdr_radio;
      target.sample_rate = config.radios[index].sample_rate_hz;
      auto controller = session.runtime->add_remote_controller(target);
      if (!controller)
        throw std::runtime_error("processor remote controller setup failed");
      session.controller = *controller;
    }
    worker = std::jthread([this] { run(); });
  }

  ~Implementation() {
    stop.store(true);
    if (worker.joinable())
      worker.join();
  }

  void count(std::uint64_t ProcessorControllerMetrics::*field) {
    std::lock_guard lock(mutex);
    ++(metrics_.*field);
  }

  bool progress(Session &session, std::chrono::milliseconds budget) {
    const auto deadline = Clock::now() + budget;
    while (!stop.load() && Clock::now() < deadline) {
      const auto elapsed = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - origin)
              .count());
      const auto second = elapsed / 1'000'000'000;
      if (second > session.last_clock_second) {
        auto pulse_time = clock_origin;
        pulse_time.seconds += second;
        if (!session.runtime->observe_pps({second * 1'000'000'000}, pulse_time))
          return false;
        session.last_clock_second = second;
      }
      auto result = session.runtime->progress(
          {elapsed});
      if (!result && !result.error().retryable)
        return false;
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return true;
  }

  bool wait(Session &session, vita::TransactionHandle handle,
            vita::WaitEvidence evidence, std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (!stop.load() && Clock::now() < deadline) {
      if (!progress(session, std::chrono::milliseconds(1))) {
        static_cast<void>(session.controller->release(handle));
        return false;
      }
      if (accepted(*session.controller, handle, evidence)) {
        return session.controller->release(handle).has_value();
      }
    }
    static_cast<void>(session.controller->release(handle));
    return false;
  }

  bool wait_state(Session &session, vita::TransactionHandle handle,
                  std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (!stop.load() && Clock::now() < deadline) {
      if (!progress(session, std::chrono::milliseconds(1))) {
        static_cast<void>(session.controller->release(handle));
        return false;
      }
      auto state = session.controller->state(handle);
      if (state && *state)
        return session.controller->release(handle).has_value();
    }
    static_cast<void>(session.controller->release(handle));
    return false;
  }

  bool reconcile(Session &session) {
    const auto before = read_status(session.transport.config, config.io_timeout);
    if (!before || !before->ready) {
      count(&ProcessorControllerMetrics::status_failures);
      return false;
    }
    count(&ProcessorControllerMetrics::connection_attempts);
    session.transport.instance->request_reconnect();
    const auto deadline = Clock::now() + config.io_timeout;
    while (!stop.load() && Clock::now() < deadline &&
           !session.transport.instance->connected())
      if (!progress(session, std::chrono::milliseconds(1)))
        return false;
    if (!session.transport.instance->connected())
      return false;
    const auto after = read_status(session.transport.config, config.io_timeout);
    if (!after || before->boot_id != after->boot_id || !after->active ||
        !after->generation) {
      count(&ProcessorControllerMetrics::status_failures);
      return false;
    }
    const bool boot_changed = session.status &&
                              session.status->boot_id != after->boot_id;
    if (boot_changed) {
      session.configured = false;
      session.started = false;
      count(&ProcessorControllerMetrics::boot_changes);
    }
    session.status = after;
    session.reconciled_generation = session.transport.instance->generation();
    auto status = session.controller->status();
    if (!status || !wait_state(session, *status, config.io_timeout)) {
      count(&ProcessorControllerMetrics::protocol_failures);
      const auto observation = status
                   ? session.controller->observation(*status)
                   : vita::Result<vita::runtime::transaction::Observation>{
                     std::unexpected(vita::Error{
                     vita::ErrorCode::invalid_state})};
      *events << nlohmann::json{
                     {"type", "controller_error"},
                     {"radio_id", session.transport.config.id},
                     {"reason", "vrt_status_failed"},
                     {"received_packets",
                      session.transport.instance->received_packets()},
                     {"route_failures", session.transport.instance->route_failures()},
                     {"observation_kind",
                      observation ? static_cast<unsigned>(observation->kind)
                                  : 999u},
                     {"validation_accepted",
                      observation && observation->validation_accepted},
                     {"confirms_execution",
                      observation && observation->confirms_execution}}
                     .dump()
              << '\n';
      return false;
    }
    session.last_liveness = Clock::now();
    session.consecutive_failures = 0;
    count(session.reconciled_generation > 1
              ? &ProcessorControllerMetrics::reconnects
              : &ProcessorControllerMetrics::connections);
    return true;
  }

  bool liveness(Session &session) {
    if (Clock::now() - session.last_liveness < config.liveness_interval)
      return true;
    auto transaction = session.controller->status();
    if (!transaction || !wait_state(session, *transaction, config.io_timeout)) {
      count(&ProcessorControllerMetrics::protocol_failures);
      session.transport.instance->request_reconnect();
      return false;
    }
    session.last_liveness = Clock::now();
    return true;
  }

  bool configure(Session &session) {
    vita::SdrRadioSettings settings;
    settings.center_frequency =
        *vita::Hertz::from_integer(session.transport.config.center_hz);
    settings.sample_rate =
        *vita::Hertz::from_integer(session.transport.config.sample_rate_hz);
    settings.bandwidth =
        *vita::Hertz::from_integer(session.transport.config.bandwidth_hz);
    settings.gain = {session.transport.config.gain_db_q7, 0};
    auto transaction = session.controller->configure(settings);
    if (!transaction || !wait(session, *transaction, vita::WaitEvidence::execution,
                              config.io_timeout)) {
      count(&ProcessorControllerMetrics::protocol_failures);
      return false;
    }
    session.configured = true;
    count(&ProcessorControllerMetrics::configurations);
    return true;
  }

  bool restart_after_boot_change(Session &session) {
    auto epoch = utc_now();
    epoch.seconds += 5;
    vita::CommandOptions options;
    options.timeout_ns = 10'000'000'000ULL;
    auto transaction = session.controller->start(epoch, options);
    if (!transaction) {
      count(&ProcessorControllerMetrics::protocol_failures);
      return false;
    }
    count(&ProcessorControllerMetrics::starts_submitted);
    const auto admission_deadline = std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::seconds(epoch.seconds - 1) +
            std::chrono::nanoseconds(epoch.picoseconds / 1000)));
    const auto remaining = admission_deadline - std::chrono::system_clock::now();
    if (remaining <= std::chrono::milliseconds::zero() ||
        !wait(session, *transaction, vita::WaitEvidence::validation,
              std::chrono::duration_cast<std::chrono::milliseconds>(remaining))) {
      count(&ProcessorControllerMetrics::protocol_failures);
      return false;
    }
    count(&ProcessorControllerMetrics::starts_admitted);
    session.started = true;
    *events << nlohmann::json{{"type", "controller"},
                              {"state", "radio_restarted"},
                              {"radio_id", session.transport.config.id},
                              {"start_epoch", {{"seconds", epoch.seconds},
                                                {"picoseconds", epoch.picoseconds}}}}
                   .dump()
            << '\n';
    return true;
  }

  bool coordinate_start() {
    auto epoch = utc_now();
    epoch.seconds += 5;
    std::array<std::optional<vita::TransactionHandle>, 4> transactions;
    for (std::size_t index = 0; index < sessions.size(); ++index) {
      vita::CommandOptions options;
      options.timeout_ns = 10'000'000'000ULL;
      auto transaction = sessions[index].controller->start(epoch, options);
      if (!transaction) {
        count(&ProcessorControllerMetrics::protocol_failures);
        return false;
      }
      transactions[index] = *transaction;
      count(&ProcessorControllerMetrics::starts_submitted);
    }
    const auto admission_deadline = std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::seconds(epoch.seconds - 1) +
            std::chrono::nanoseconds(epoch.picoseconds / 1000)));
    for (std::size_t index = 0; index < sessions.size(); ++index) {
      const auto remaining = admission_deadline - std::chrono::system_clock::now();
      if (remaining <= std::chrono::milliseconds::zero() ||
          !wait(sessions[index], *transactions[index],
                vita::WaitEvidence::validation,
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining))) {
        count(&ProcessorControllerMetrics::protocol_failures);
        for (std::size_t cancel = 0; cancel < index; ++cancel)
          static_cast<void>(sessions[cancel].controller->cancel(
              *transactions[cancel],
              vita::QuerySelection{vita::QueryField::streaming}));
        return false;
      }
      count(&ProcessorControllerMetrics::starts_admitted);
    }
    for (auto &session : sessions)
      session.started = true;
    coordinated_.store(true);
    *events << nlohmann::json{{"type", "controller"},
                              {"state", "coordinated"},
                              {"start_epoch", {{"seconds", epoch.seconds},
                                                {"picoseconds", epoch.picoseconds}}}}
                   .dump()
            << '\n';
    return true;
  }

  void run() {
    std::chrono::milliseconds backoff{100};
    while (!stop.load()) {
      bool ready = true;
      for (auto &session : sessions) {
        if (stop.load())
          break;
        if (!session.transport.instance->connected() ||
            session.transport.instance->generation() !=
                session.reconciled_generation) {
          if (!reconcile(session)) {
            ++session.consecutive_failures;
            if (session.consecutive_failures == config.retry_limit)
              *events << nlohmann::json{{"type", "controller_error"},
                                        {"radio_id", session.transport.config.id},
                                        {"reason", "retry_limit_reached"},
                                        {"retry_count", config.retry_limit}}
                             .dump()
                      << '\n';
            ready = false;
            continue;
          }
        }
        if (!session.configured && !configure(session))
          ready = false;
        if (coordinated_.load() && session.configured && !session.started &&
            !restart_after_boot_change(session))
          ready = false;
        if (session.configured && !liveness(session))
          ready = false;
      }
      if (ready && !coordinated_.load() && !coordinate_start())
        ready = false;
      for (auto &session : sessions)
        if (!progress(session, std::chrono::milliseconds(1))) {
          count(&ProcessorControllerMetrics::protocol_failures);
          ready = false;
        }
      if (ready)
        backoff = std::chrono::milliseconds(100);
      else {
        std::this_thread::sleep_for(backoff);
        backoff = std::min(backoff * 2, config.maximum_backoff);
      }
    }
  }
};

ProcessorController::ProcessorController(ProcessorControllerConfig config,
                                         std::ostream &events)
    : implementation_(std::make_unique<Implementation>(std::move(config), events)) {}
ProcessorController::~ProcessorController() = default;

ProcessorControllerMetrics ProcessorController::metrics() const noexcept {
  std::lock_guard lock(implementation_->mutex);
  return implementation_->metrics_;
}

bool ProcessorController::coordinated() const noexcept {
  return implementation_->coordinated_.load();
}
} // namespace sdr