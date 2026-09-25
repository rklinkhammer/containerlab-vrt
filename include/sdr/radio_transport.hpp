#pragma once

#include <vita/runtime/transport/binding.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <sdr/status.hpp>

namespace sdr {
struct RadioTransportConfig {
  std::string control_host;
  std::uint16_t control_port{};
  std::string data_source_host;
  std::string data_host;
  std::uint16_t data_port{};
  std::uint32_t sid{};
  std::uint64_t controller_peer{1};
  std::uint64_t controllee_peer{2};
  std::size_t control_queue{64};
  int tcp_send_buffer{};
  std::size_t maximum_tcp_write_bytes{};
  std::chrono::milliseconds io_timeout{std::chrono::seconds(2)};
  std::chrono::milliseconds stale_timeout{std::chrono::seconds(5)};
};

struct RadioTransportMetrics {
  std::uint64_t accepted_connections{};
  std::uint64_t rejected_connections{};
  std::uint64_t received_packets{};
  std::uint64_t completed_submissions{};
  std::uint64_t failed_submissions{};
  std::uint64_t partial_writes{};
  std::uint64_t write_retries{};
};

class RadioTransport;

struct RadioTransportFactory {
  RadioTransportConfig config;
  ControlSlot *control{};
  std::shared_ptr<RadioTransport> instance;
};

vita::runtime::transport::TransportFactory
radio_transport_factory(RadioTransportFactory &setup) noexcept;

class RadioTransport {
public:
  RadioTransport(const RadioTransport &) = delete;
  RadioTransport &operator=(const RadioTransport &) = delete;
  ~RadioTransport();

  std::uint16_t control_port() const noexcept;
  RadioTransportMetrics metrics() const noexcept;
  bool connected() const noexcept;
  void disconnect_controller() noexcept;
  void detach() noexcept;

private:
  friend vita::runtime::transport::TransportFactory
  radio_transport_factory(RadioTransportFactory &) noexcept;
  struct Implementation;
  explicit RadioTransport(std::unique_ptr<Implementation> implementation);
  std::unique_ptr<Implementation> implementation_;
};
} // namespace sdr