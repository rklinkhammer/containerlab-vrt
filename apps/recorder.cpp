#include <sdr/recorder.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef __linux__
#include <arpa/inet.h>
#include <cerrno>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
#ifdef __linux__
volatile std::sig_atomic_t running = 1;
void stop(int) { running = 0; }

class PacketSocket {
public:
  explicit PacketSocket(const std::string &interface) {
    const unsigned interface_index = if_nametoindex(interface.c_str());
    if (interface_index == 0)
      throw std::runtime_error("capture interface not found");
    descriptor_ = socket(AF_PACKET, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC,
                         htons(ETH_P_ALL));
    if (descriptor_ < 0)
      throw std::runtime_error(std::string("cannot create packet socket: ") +
                               std::strerror(errno));
    const int receive_bytes = sdr::recorder::socket_receive_bytes;
    if (setsockopt(descriptor_, SOL_SOCKET, SO_RCVBUF, &receive_bytes,
                   sizeof(receive_bytes)) < 0)
      fail("cannot set packet socket receive bound");
    sockaddr_ll address{};
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(ETH_P_ALL);
    address.sll_ifindex = static_cast<int>(interface_index);
    if (bind(descriptor_, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) < 0)
      fail("cannot bind packet socket");
  }
  ~PacketSocket() {
    if (descriptor_ >= 0)
      close(descriptor_);
  }
  PacketSocket(const PacketSocket &) = delete;
  PacketSocket &operator=(const PacketSocket &) = delete;
  int descriptor() const noexcept { return descriptor_; }

private:
  [[noreturn]] void fail(const char *message) {
    const auto error = std::string(message) + ": " + std::strerror(errno);
    close(descriptor_);
    descriptor_ = -1;
    throw std::runtime_error(error);
  }
  int descriptor_{-1};
};

void collect_kernel_statistics(int descriptor,
                               sdr::recorder::Metrics &metrics) {
  tpacket_stats statistics{};
  socklen_t length = sizeof(statistics);
  if (getsockopt(descriptor, SOL_PACKET, PACKET_STATISTICS, &statistics,
                 &length) == 0)
    metrics.observe_kernel(statistics.tp_packets, statistics.tp_drops);
  else
    ++metrics.receive_errors;
}

int run(const sdr::recorder::Options &options) {
  const auto interface_deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(30);
  while (if_nametoindex(options.interface.c_str()) == 0) {
    if (std::chrono::steady_clock::now() >= interface_deadline)
      throw std::runtime_error("capture interface did not appear before timeout");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  PacketSocket socket(options.interface);
  std::unique_ptr<sdr::recorder::PcapWriter> pcap;
  if (options.pcap_path) {
    const auto parent = options.pcap_path->parent_path();
    if (!parent.empty())
      std::filesystem::create_directories(parent);
    pcap = std::make_unique<sdr::recorder::PcapWriter>(*options.pcap_path,
                                                       options.capture_bytes);
  }
  std::signal(SIGINT, stop);
  std::signal(SIGTERM, stop);
  const auto started = std::chrono::steady_clock::now();
  const auto deadline = options.duration
                            ? std::optional(started + *options.duration)
                            : std::nullopt;
  auto next_report = started;
  sdr::recorder::Metrics metrics;
  if (pcap)
    metrics.pcap_bytes = pcap->bytes();
  std::array<std::byte, sdr::recorder::receive_buffer_bytes> frame{};
    while (running != 0 &&
      (!deadline || std::chrono::steady_clock::now() < *deadline)) {
    pollfd item{socket.descriptor(), POLLIN, 0};
    const int ready = poll(&item, 1, 200);
    if (ready < 0 && errno != EINTR)
      throw std::runtime_error(std::string("packet socket poll failed: ") +
                               std::strerror(errno));
    if (ready > 0 && (item.revents & (POLLERR | POLLNVAL)) != 0)
      throw std::runtime_error("packet socket reported a terminal error");
    if (ready > 0 && (item.revents & POLLIN) != 0) {
      while (true) {
        iovec vector{frame.data(), frame.size()};
        msghdr message{};
        message.msg_iov = &vector;
        message.msg_iovlen = 1;
        const auto received = recvmsg(socket.descriptor(), &message, MSG_TRUNC);
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
          break;
        if (received < 0) {
          if (errno == EINTR)
            continue;
          ++metrics.receive_errors;
          break;
        }
        const auto wire_size = static_cast<std::size_t>(received);
        const auto copied_size = std::min(wire_size, frame.size());
        metrics.observe_frame(wire_size, copied_size);
        if (pcap && !metrics.pcap_limit_reached) {
          try {
            if (pcap->write(std::span(frame).first(copied_size), wire_size,
                            std::chrono::system_clock::now()))
              ++metrics.pcap_frames;
            metrics.pcap_bytes = pcap->bytes();
            metrics.pcap_limit_reached = pcap->limit_reached();
          } catch (const std::exception &) {
            ++metrics.pcap_io_errors;
            pcap.reset();
          }
        }
      }
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_report) {
      collect_kernel_statistics(socket.descriptor(), metrics);
      sdr::recorder::write_metrics(options, metrics, started, "running");
      next_report = now + std::chrono::seconds(1);
    }
  }
  collect_kernel_statistics(socket.descriptor(), metrics);
  if (pcap) {
    try {
      pcap->finish();
    } catch (const std::exception &) {
      ++metrics.pcap_io_errors;
    }
  }
  sdr::recorder::write_metrics(options, metrics, started, "stopped");
  return 0;
}
#else
int run(const sdr::recorder::Options &) {
  throw std::runtime_error("AF_PACKET recording is supported only on Linux");
}
#endif
} // namespace

int main(int argc, char **argv) {
  try {
    return run(sdr::recorder::parse_options(argc, argv));
  } catch (const std::exception &error) {
    std::cerr << "recorder: " << error.what() << '\n';
    return 2;
  }
}
