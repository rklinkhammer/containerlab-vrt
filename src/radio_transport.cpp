#include <sdr/radio_transport.hpp>

#include <vita/codec/packet.hpp>
#include <vita/runtime/transaction/cam.hpp>
#include <vita/runtime/transport/framing.hpp>
#include <vita/runtime/transport/stream_framer.hpp>

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstring>
#include <optional>
#include <thread>
#include <utility>

namespace sdr {
namespace {
using Clock = std::chrono::steady_clock;
using vita::runtime::transport::Association;
using vita::runtime::transport::RejectedSubmission;
using vita::runtime::transport::TxSubmission;
using vita::runtime::transport::TxToken;

constexpr std::size_t data_slots = 256;
constexpr std::size_t total_slots = 320;

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

vita::Error socket_error(bool retryable = false) noexcept {
  vita::Error error{vita::ErrorCode::invalid_state};
  error.retryable = retryable;
  error.stage = vita::ErrorStage::transport;
  error.native_error = errno;
  return error;
}

bool nonblocking(int descriptor) noexcept {
  const auto flags = fcntl(descriptor, F_GETFL, 0);
  return flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

vita::Result<Descriptor> listener(const std::string &host,
                                  std::uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  addrinfo *raw = nullptr;
  const auto service = std::to_string(port);
  if (getaddrinfo(host.c_str(), service.c_str(), &hints, &raw) != 0)
    return std::unexpected(vita::Error{vita::ErrorCode::invalid_argument});
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(raw,
                                                               freeaddrinfo);
  Descriptor result(socket(raw->ai_family, raw->ai_socktype, raw->ai_protocol));
  const int reuse = 1;
  if (result.value < 0 ||
      setsockopt(result.value, SOL_SOCKET, SO_REUSEADDR, &reuse,
                 sizeof(reuse)) < 0 ||
      bind(result.value, raw->ai_addr, raw->ai_addrlen) < 0 ||
      listen(result.value, 4) < 0 || !nonblocking(result.value))
    return std::unexpected(socket_error());
  return result;
}

vita::Result<Descriptor> udp_destination(const std::string &source_host,
                                         const std::string &host,
                                         std::uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo *raw = nullptr;
  const auto service = std::to_string(port);
  if (getaddrinfo(host.c_str(), service.c_str(), &hints, &raw) != 0)
    return std::unexpected(vita::Error{vita::ErrorCode::invalid_argument});
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(raw,
                                                               freeaddrinfo);
  Descriptor result(socket(raw->ai_family, raw->ai_socktype, raw->ai_protocol));
  sockaddr_in source{};
  source.sin_family = AF_INET;
  if (result.value < 0 ||
      inet_pton(AF_INET, source_host.c_str(), &source.sin_addr) != 1)
    return std::unexpected(vita::Error{vita::ErrorCode::invalid_argument});
  const auto deadline = Clock::now() + std::chrono::seconds(30);
  while (bind(result.value, reinterpret_cast<const sockaddr *>(&source),
              sizeof(source)) < 0) {
    if (errno != EADDRNOTAVAIL || Clock::now() >= deadline)
      return std::unexpected(socket_error());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (
      connect(result.value, raw->ai_addr, raw->ai_addrlen) < 0 ||
      !nonblocking(result.value))
    return std::unexpected(socket_error());
  return result;
}
} // namespace

struct RadioTransport::Implementation {
  struct Slot {
    std::optional<TxSubmission> submission;
    vita::runtime::AdmissionBundle credits;
    std::size_t segment{};
    std::size_t offset{};
    std::uint64_t generation{1};
    Clock::time_point deadline{};
    bool data{};
    bool status_reply{};
  };

  RadioTransportConfig config;
  ControlSlot *control;
  Descriptor listener;
  Descriptor udp;
  Descriptor tcp;
  vita::runtime::AdmissionPool *admission{};
  vita::runtime::CounterRegistry<128> *counters{};
  std::optional<vita::runtime::transport::StreamIngress<1024, 128>> ingress;
  vita::runtime::transport::StreamFramer<1024> liveness;
  std::array<Slot, total_slots> slots{};
  std::optional<vita::runtime::PeerSession> local;
  std::optional<vita::runtime::PeerSession> remote;
  std::optional<std::uint64_t> connection_generation;
  Clock::time_point connected_at{};
  Clock::time_point input_activity{};
  std::size_t cursor{};
  bool open{true};
  bool accepting{true};
  bool progressing{};
  bool pending_status_request{};
  RadioTransportMetrics metrics_{};

  Implementation(RadioTransportConfig value, ControlSlot &slot,
                 Descriptor listen_socket, Descriptor data_socket,
                 vita::runtime::transport::HostBindings host)
      : config(std::move(value)), control(&slot),
        listener(std::move(listen_socket)), udp(std::move(data_socket)),
        admission(&host.admission), counters(&host.counters) {
    ingress.emplace(host.routes, std::move(host.rx_data),
                    std::move(host.rx_control), std::move(host.rx_cancellation),
                    vita::runtime::PeerSession{} , 1024);
  }

  bool is_status(vita::Bytes wire, bool reply) noexcept {
    if (reply) {
      auto envelope = vita::codec::decode_envelope(wire);
      return envelope &&
             vita::codec::is_command(envelope->envelope.type) &&
             envelope->envelope.command && envelope->envelope.ack &&
             !envelope->envelope.cancel &&
             (envelope->envelope.command->cam & (1u << 18)) != 0;
    }
    auto packet = vita::codec::decode_packet(wire);
    if (!packet || !vita::codec::is_command(packet->envelope.envelope.type) ||
        !packet->envelope.envelope.command ||
        packet->envelope.envelope.ack ||
        packet->envelope.envelope.cancel)
      return false;
    auto parsed = vita::runtime::transaction::Cam::parse(
        packet->envelope.envelope,
        vita::runtime::transaction::Profile::sdr_radio);
    if (!parsed || parsed->action != 0 || !parsed->request_s ||
        packet->fields.size() != 5)
      return false;
    std::uint8_t fields = 0;
    for (std::size_t index = 0; index < packet->fields.size(); ++index) {
      const auto id = packet->fields[index].id;
      if (id == vita::RFReferenceFrequency::id)
        fields |= 1u << 0;
      else if (id == vita::SampleRate::id)
        fields |= 1u << 1;
      else if (id == vita::Bandwidth::id)
        fields |= 1u << 2;
      else if (id == vita::Gain::id)
        fields |= 1u << 3;
      else if (id == vita::DiscreteIO32::id)
        fields |= 1u << 4;
    }
    return fields == 0x1f;
  }

  bool is_status_reply(const vita::memory::TxStorage &storage) noexcept {
    if (storage.byte_size() > 1024)
      return false;
    std::array<std::byte, 1024> wire{};
    std::size_t offset = 0;
    for (std::size_t index = 0; index < storage.segment_count(); ++index) {
      auto segment = storage.segment(index);
      if (!segment)
        return false;
      std::memcpy(wire.data() + offset, segment->data(), segment->size());
      offset += segment->size();
    }
    return is_status(vita::Bytes{wire}.first(offset), true);
  }

  static vita::Result<void> inspect_liveness(void *context,
                                              vita::Bytes wire) noexcept {
    auto &self = *static_cast<Implementation *>(context);
    if (self.is_status(wire, false))
      self.pending_status_request = true;
    return {};
  }

  void finish(Slot &slot, vita::runtime::CompletionResult result) noexcept {
    if (result.status == vita::runtime::CompletionStatus::succeeded)
      ++metrics_.completed_submissions;
    else
      ++metrics_.failed_submissions;
    if (slot.status_reply && pending_status_request && connection_generation &&
        result.status == vita::runtime::CompletionStatus::succeeded) {
      control->observe_liveness(*connection_generation, Clock::now());
      pending_status_request = false;
    }
    slot.submission->completion.publish(result);
    slot.submission.reset();
    slot.credits.reset();
    slot.segment = slot.offset = 0;
    ++slot.generation;
  }

  void fail_control() noexcept {
    if (tcp.value >= 0) {
      tcp.reset();
      if (ingress)
        static_cast<void>(ingress->disconnect());
      liveness.disconnect();
    }
    if (connection_generation) {
      control->release(*connection_generation);
      connection_generation.reset();
    }
    pending_status_request = false;
    for (auto &slot : slots)
      if (slot.submission && !slot.data) {
        vita::runtime::CompletionResult result;
        result.status = vita::runtime::CompletionStatus::failed;
        result.error = vita::Error{vita::ErrorCode::invalid_state};
        finish(slot, result);
      }
  }

  vita::Result<bool> accept_controller() noexcept {
    if (!accepting || listener.value < 0)
      return false;
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    const auto descriptor = accept(listener.value,
                                   reinterpret_cast<sockaddr *>(&address),
                                   &length);
    if (descriptor < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return false;
      return std::unexpected(socket_error());
    }
    Descriptor candidate(descriptor);
    if (!nonblocking(candidate.value))
      return std::unexpected(socket_error());
    if (config.tcp_send_buffer > 0 &&
        setsockopt(candidate.value, SOL_SOCKET, SO_SNDBUF,
                   &config.tcp_send_buffer,
                   sizeof(config.tcp_send_buffer)) < 0)
      return std::unexpected(socket_error());
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (setsockopt(candidate.value, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                   sizeof(enabled)) < 0)
      return std::unexpected(socket_error());
#endif
    if (tcp.value >= 0) {
      ++metrics_.rejected_connections;
      return true;
    }
    const auto generation = control->acquire(Clock::now());
    if (!generation || !remote)
      return true;
    tcp = std::move(candidate);
    connection_generation = *generation;
    connected_at = input_activity = Clock::now();
    if (ingress) {
      auto reset = ingress->reconnect(*remote);
      if (!reset) {
        fail_control();
        return std::unexpected(reset.error());
      }
    }
    liveness.reconnect();
    ++metrics_.accepted_connections;
    return true;
  }

  vita::Result<bool> receive_control() noexcept {
    if (tcp.value < 0)
      return false;
    std::array<std::byte, 2048> buffer{};
    const auto count = recv(tcp.value, buffer.data(), buffer.size(), 0);
    if (count == 0) {
      fail_control();
      return true;
    }
    if (count < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return false;
      fail_control();
      return true;
    }
    input_activity = Clock::now();
    const vita::Bytes input{buffer.data(), static_cast<std::size_t>(count)};
    auto delivered = ingress->feed(input);
    auto observed = liveness.feed(input, this, inspect_liveness);
    if (!delivered || !observed) {
      fail_control();
      return true;
    }
    metrics_.received_packets += *delivered;
    return true;
  }

  vita::Result<bool> transmit(std::size_t index) noexcept {
    auto &slot = slots[index];
    if (!slot.submission)
      return false;
    if (Clock::now() >= slot.deadline || (!slot.data && tcp.value < 0)) {
      vita::runtime::CompletionResult result;
      result.status = vita::runtime::CompletionStatus::failed;
      result.error = vita::Error{vita::ErrorCode::invalid_state};
      finish(slot, result);
      return true;
    }
    std::array<iovec, 3> vectors{};
    std::size_t count = 0;
    std::size_t write_budget =
        !slot.data && config.maximum_tcp_write_bytes
            ? config.maximum_tcp_write_bytes
            : SIZE_MAX;
    for (std::size_t segment = slot.segment;
         segment < slot.submission->storage.segment_count() && write_budget;
         ++segment) {
      auto bytes = slot.submission->storage.segment(segment);
      if (!bytes)
        return std::unexpected(bytes.error());
      const auto offset = segment == slot.segment ? slot.offset : 0;
      const auto length = std::min(bytes->size() - offset, write_budget);
      vectors[count++] = {const_cast<std::byte *>(bytes->data() + offset),
                          length};
      write_budget -= length;
    }
    msghdr message{};
    message.msg_iov = vectors.data();
    message.msg_iovlen = count;
    const auto descriptor = slot.data ? udp.value : tcp.value;
  #ifdef MSG_NOSIGNAL
    const auto sent = sendmsg(descriptor, &message, MSG_NOSIGNAL);
  #else
    const auto sent = sendmsg(descriptor, &message, 0);
  #endif
    if (sent < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        ++metrics_.write_retries;
        return false;
      }
      if (!slot.data)
        fail_control();
      else {
        vita::runtime::CompletionResult result;
        result.status = vita::runtime::CompletionStatus::failed;
        result.error = socket_error();
        finish(slot, result);
      }
      return true;
    }
    std::size_t consumed = static_cast<std::size_t>(sent);
    if (slot.data && consumed != slot.submission->storage.byte_size()) {
      vita::runtime::CompletionResult result;
      result.status = vita::runtime::CompletionStatus::failed;
      result.error = vita::Error{vita::ErrorCode::short_output};
      finish(slot, result);
      return true;
    }
    while (consumed && slot.segment < slot.submission->storage.segment_count()) {
      auto bytes = slot.submission->storage.segment(slot.segment);
      if (!bytes)
        return std::unexpected(bytes.error());
      const auto remaining = bytes->size() - slot.offset;
      if (consumed < remaining) {
        slot.offset += consumed;
        consumed = 0;
      } else {
        consumed -= remaining;
        ++slot.segment;
        slot.offset = 0;
      }
    }
    if (slot.segment != slot.submission->storage.segment_count()) {
      ++metrics_.partial_writes;
      return true;
    }
    vita::runtime::CompletionResult result;
    result.status = vita::runtime::CompletionStatus::succeeded;
    result.value = slot.submission->storage.byte_size();
    finish(slot, result);
    return true;
  }

  vita::Result<bool> progress_next() noexcept {
    if (!open)
      return false;
    if (progressing)
      return std::unexpected(vita::Error{vita::ErrorCode::would_deadlock});
    struct Guard {
      bool &value;
      ~Guard() { value = false; }
    } guard{progressing};
    progressing = true;
    if (tcp.value >= 0) {
      const auto now = Clock::now();
      if ((ingress->stalled() && now - input_activity >= config.io_timeout) ||
          (now - connected_at >= config.stale_timeout &&
           control->snapshot().occupied &&
           control->release_if_stale(now, config.stale_timeout))) {
        fail_control();
        return true;
      }
    }
    auto accepted = accept_controller();
    if (!accepted || *accepted)
      return accepted;
    auto received = receive_control();
    if (!received || *received)
      return received;
    for (std::size_t attempt = 0; attempt < slots.size(); ++attempt) {
      const auto selected = cursor++ % slots.size();
      if (slots[selected].submission)
        return transmit(selected);
    }
    return false;
  }
};

RadioTransport::RadioTransport(std::unique_ptr<Implementation> implementation)
    : implementation_(std::move(implementation)) {}
RadioTransport::~RadioTransport() { detach(); }

std::uint16_t RadioTransport::control_port() const noexcept {
  if (!implementation_ || implementation_->listener.value < 0)
    return 0;
  sockaddr_in address{};
  socklen_t length = sizeof(address);
  if (getsockname(implementation_->listener.value,
                  reinterpret_cast<sockaddr *>(&address), &length) < 0)
    return 0;
  return ntohs(address.sin_port);
}

RadioTransportMetrics RadioTransport::metrics() const noexcept {
  return implementation_ ? implementation_->metrics_ : RadioTransportMetrics{};
}

bool RadioTransport::connected() const noexcept {
  return implementation_ && implementation_->tcp.value >= 0;
}

void RadioTransport::disconnect_controller() noexcept {
  if (implementation_)
    implementation_->fail_control();
}

void RadioTransport::detach() noexcept {
  if (!implementation_ || !implementation_->open)
    return;
  implementation_->open = false;
  implementation_->fail_control();
  for (auto &slot : implementation_->slots)
    if (slot.submission) {
      vita::runtime::CompletionResult result;
      result.status = vita::runtime::CompletionStatus::failed;
      result.error = vita::Error{vita::ErrorCode::invalid_state};
      implementation_->finish(slot, result);
    }
  implementation_->ingress.reset();
  implementation_->admission = nullptr;
  implementation_->counters = nullptr;
  implementation_->listener.reset();
  implementation_->udp.reset();
}

vita::runtime::transport::TransportFactory
radio_transport_factory(RadioTransportFactory &setup) noexcept {
  vita::runtime::transport::Capabilities capabilities;
  capabilities.max_packet_bytes = 8192;
  capabilities.max_tx_segments = 3;
  capabilities.max_rx_fragments = 1;
  capabilities.reserved_control_slots = 32;
  capabilities.reserved_cancellation_slots = 32;
  capabilities.per_stream_data_limit = data_slots;
  vita::runtime::transport::TransportFactory factory;
  factory.context = &setup;
  factory.required_bytes = sizeof(RadioTransport) +
                           sizeof(RadioTransport::Implementation) + 128;
  factory.slot_capacity = total_slots;
  factory.capabilities = capabilities;
  factory.create = [](void *context,
                      vita::runtime::transport::HostBindings host) noexcept
      -> vita::Result<vita::runtime::transport::TransportBinding> {
    auto &setup = *static_cast<RadioTransportFactory *>(context);
    if (!setup.control || setup.config.control_host.empty() ||
        setup.config.data_source_host.empty() || setup.config.data_host.empty() ||
        !setup.config.sid ||
        !setup.config.data_port || setup.config.control_queue == 0 ||
        setup.config.control_queue > total_slots - data_slots ||
        setup.config.tcp_send_buffer < 0 ||
        setup.config.maximum_tcp_write_bytes > 4096 ||
        setup.config.io_timeout <= std::chrono::milliseconds::zero() ||
        setup.config.io_timeout > std::chrono::seconds(2) ||
        setup.config.stale_timeout <= std::chrono::milliseconds::zero() ||
        setup.config.stale_timeout > std::chrono::seconds(5))
      return std::unexpected(vita::Error{vita::ErrorCode::invalid_argument});
    try {
      auto listen_socket = listener(setup.config.control_host,
                                    setup.config.control_port);
      auto data_socket = udp_destination(setup.config.data_source_host,
                     setup.config.data_host,
                                         setup.config.data_port);
      if (!listen_socket || !data_socket)
        return std::unexpected(!listen_socket ? listen_socket.error()
                                              : data_socket.error());
      auto implementation = std::make_unique<RadioTransport::Implementation>(
          setup.config, *setup.control, std::move(*listen_socket),
          std::move(*data_socket), std::move(host));
      auto owner = std::shared_ptr<RadioTransport>(
          new RadioTransport(std::move(implementation)));
      auto *transport = owner->implementation_.get();
      vita::runtime::transport::TransportBinding binding;
      binding.owner = owner;
      binding.context = transport;
      binding.metadata_bytes = sizeof(RadioTransport) +
                               sizeof(RadioTransport::Implementation) + 128;
      binding.slot_capacity = total_slots;
      binding.capabilities = radio_transport_factory(setup).capabilities;
      binding.send = [](void *pointer, TxSubmission &&submission) noexcept
          -> std::expected<TxToken, RejectedSubmission> {
        auto &self = *static_cast<RadioTransport::Implementation *>(pointer);
        auto reject = [&](vita::Error error)
            -> std::expected<TxToken, RejectedSubmission> {
          return std::unexpected(
              RejectedSubmission{error, std::move(submission)});
        };
        if (!self.open || !self.local || submission.source != *self.local ||
            !submission.completion.is_reserved())
          return reject(vita::Error{vita::ErrorCode::invalid_state});
        auto framing = vita::runtime::transport::inspect(submission.storage);
        if (!framing)
          return reject(framing.error());
        const bool data = !vita::codec::is_command(framing->envelope.type);
        if ((!data && self.tcp.value < 0) ||
            framing->packet_bytes > (data ? 8192u : 1024u) ||
            submission.counter.sender != submission.source.peer ||
            submission.counter.stream_id != framing->envelope.stream_id ||
            submission.counter.type != framing->envelope.type)
          return reject(vita::Error{vita::ErrorCode::identity_conflict, 8});
        const auto first = data ? std::size_t{0} : data_slots;
        const auto last = data ? data_slots
                               : data_slots + self.config.control_queue;
        std::size_t selected = total_slots;
        for (auto index = first; index < last; ++index)
          if (!self.slots[index].submission) {
            selected = index;
            break;
          }
        if (selected == total_slots)
          return reject(vita::Error{vita::ErrorCode::capacity_exhausted});
        const bool supplied = submission.completion_credit.held(
                                  vita::runtime::Resource::completion) == 1;
        if (!supplied || !self.admission->owns(submission.completion_credit))
          return reject(vita::Error{vita::ErrorCode::invalid_argument});
        vita::runtime::AdmissionRequest request;
        request.need(data ? vita::runtime::Resource::data_queue
                          : framing->envelope.cancel
                                ? vita::runtime::Resource::cancellation_queue
                                : vita::runtime::Resource::ordinary_queue);
        auto credit = self.admission->acquire(request);
        if (!credit)
          return reject(credit.error());
        auto committed = self.counters->accept(submission.counter,
                                               framing->envelope.packet_count);
        if (!committed)
          return reject(committed.error());
        auto &slot = self.slots[selected];
        slot.submission.emplace(std::move(submission));
        slot.credits = std::move(*credit);
        slot.segment = slot.offset = 0;
        slot.deadline = Clock::now() + self.config.io_timeout;
        slot.data = data;
        slot.status_reply = !data &&
                            self.is_status_reply(slot.submission->storage);
        return TxToken{selected, slot.generation};
      };
      binding.progress = [](void *pointer) noexcept {
        return static_cast<RadioTransport::Implementation *>(pointer)
            ->progress_next();
      };
      binding.pending = [](void *pointer, TxToken token) noexcept {
        auto &self = *static_cast<RadioTransport::Implementation *>(pointer);
        return token.slot < self.slots.size() &&
               self.slots[token.slot].generation == token.generation &&
               self.slots[token.slot].submission.has_value();
      };
      binding.close_admission = [](void *pointer) noexcept {
        auto &self = *static_cast<RadioTransport::Implementation *>(pointer);
        self.accepting = false;
        self.listener.reset();
      };
      binding.associate = [](void *pointer,
                             std::span<const Association> associations,
                             bool commit) noexcept -> vita::Result<void> {
        auto &self = *static_cast<RadioTransport::Implementation *>(pointer);
        if (associations.size() != 1 ||
            associations[0].sid != self.config.sid ||
            associations[0].local.peer != self.config.controllee_peer ||
            associations[0].remote.peer != self.config.controller_peer ||
            !associations[0].local.generation ||
            associations[0].local.generation !=
                associations[0].remote.generation)
            return std::unexpected(
              vita::Error{vita::ErrorCode::identity_conflict, 9});
        if (commit) {
          self.local = associations[0].local;
          self.remote = associations[0].remote;
        }
        return {};
      };
      binding.detach = [](void *pointer) noexcept {
        auto &self = *static_cast<RadioTransport::Implementation *>(pointer);
        self.open = false;
        self.fail_control();
        self.ingress.reset();
        self.admission = nullptr;
        self.counters = nullptr;
        self.listener.reset();
        self.udp.reset();
      };
      setup.instance = owner;
      return binding;
    } catch (...) {
      return std::unexpected(
          vita::Error{vita::ErrorCode::capacity_exhausted});
    }
  };
  return factory;
}
} // namespace sdr