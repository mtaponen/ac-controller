#pragma once
#ifdef USE_ESP32

#include "esphome/components/network/ip_address.h"
#include "esphome/components/socket/socket.h"

namespace esphome {
namespace setup_portal {

// Answers every A query with the setup hotspot address, so a phone
// opens the form instead of its usual connectivity check page.
class DNSServer {
 public:
  void start(const network::IPAddress &ip);
  void stop();
  void process_next_request();

 protected:
  void destroy_socket_();

  static constexpr size_t DNS_BUFFER_SIZE = 192;

  socket::ListenSocket *socket_{nullptr};
  network::IPAddress server_ip_;
  uint8_t buffer_[DNS_BUFFER_SIZE];
};

}  // namespace setup_portal
}  // namespace esphome

#endif
