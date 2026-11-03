#include "dns_server.h"
#ifdef USE_ESP32

#include "esphome/core/log.h"

#include <lwip/inet.h>
#include <lwip/sockets.h>

namespace esphome {
namespace setup_portal {

static const char *const TAG = "setup_portal.dns";

static constexpr uint16_t DNS_PORT = 53;
static constexpr uint16_t DNS_QR_FLAG = 1 << 15;
static constexpr uint16_t DNS_AA_FLAG = 1 << 10;
static constexpr uint16_t DNS_OPCODE_MASK = 0x7800;
static constexpr uint16_t DNS_QTYPE_A = 0x0001;
static constexpr uint16_t DNS_QCLASS_IN = 0x0001;
static constexpr uint16_t DNS_ANSWER_TTL = 300;

struct DNSHeader {
  uint16_t id;
  uint16_t flags;
  uint16_t qd_count;
  uint16_t an_count;
  uint16_t ns_count;
  uint16_t ar_count;
} __attribute__((packed));

struct DNSQuestion {
  uint16_t type;
  uint16_t dns_class;
} __attribute__((packed));

struct DNSAnswer {
  uint16_t ptr_offset;
  uint16_t type;
  uint16_t dns_class;
  uint32_t ttl;
  uint16_t addr_len;
  uint32_t ip_addr;
} __attribute__((packed));

void DNSServer::destroy_socket_() {
  delete this->socket_;
  this->socket_ = nullptr;
}

void DNSServer::start(const network::IPAddress &ip) {
  this->server_ip_ = ip;
  this->socket_ = socket::socket_ip_loop_monitored(SOCK_DGRAM, IPPROTO_UDP).release();
  if (this->socket_ == nullptr) {
    ESP_LOGE(TAG, "Socket create failed");
    return;
  }

  int enable = 1;
  this->socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  struct sockaddr_storage server_addr = {};
  socklen_t addr_len = socket::set_sockaddr_any(reinterpret_cast<struct sockaddr *>(&server_addr), sizeof(server_addr),
                                                DNS_PORT);
  if (this->socket_->bind(reinterpret_cast<struct sockaddr *>(&server_addr), addr_len) != 0) {
    ESP_LOGE(TAG, "Bind failed: %d", errno);
    this->destroy_socket_();
  }
}

void DNSServer::stop() { this->destroy_socket_(); }

void DNSServer::process_next_request() {
  if (this->socket_ == nullptr || !this->socket_->ready()) {
    return;
  }
  int fd = this->socket_->get_fd();
  if (fd < 0) {
    return;
  }

  struct sockaddr_in client_addr = {};
  socklen_t client_addr_len = sizeof(client_addr);
  ssize_t len = recvfrom(fd, this->buffer_, sizeof(this->buffer_), MSG_DONTWAIT,
                         reinterpret_cast<struct sockaddr *>(&client_addr), &client_addr_len);
  if (len < static_cast<ssize_t>(sizeof(DNSHeader) + 1)) {
    return;
  }

  auto *header = reinterpret_cast<DNSHeader *>(this->buffer_);
  uint16_t flags = ntohs(header->flags);
  uint16_t qd_count = ntohs(header->qd_count);
  if ((flags & DNS_QR_FLAG) || (flags & DNS_OPCODE_MASK) || qd_count != 1) {
    return;
  }

  uint8_t *ptr = this->buffer_ + sizeof(DNSHeader);
  uint8_t *end = this->buffer_ + len;
  while (ptr < end && *ptr != 0) {
    uint8_t label_len = *ptr;
    if (label_len > 63 || ptr + label_len + 1 > end) {
      return;
    }
    ptr += label_len + 1;
  }
  if (ptr >= end || *ptr != 0) {
    return;
  }
  ptr++;
  if (ptr + sizeof(DNSQuestion) > end) {
    return;
  }

  auto *question = reinterpret_cast<DNSQuestion *>(ptr);
  if (ntohs(question->type) != DNS_QTYPE_A || ntohs(question->dns_class) != DNS_QCLASS_IN) {
    return;
  }

  header->flags = htons(DNS_QR_FLAG | DNS_AA_FLAG);
  header->an_count = htons(1);

  size_t question_len = (ptr + sizeof(DNSQuestion)) - this->buffer_ - sizeof(DNSHeader);
  size_t answer_offset = sizeof(DNSHeader) + question_len;
  if (answer_offset + sizeof(DNSAnswer) > sizeof(this->buffer_)) {
    ESP_LOGW(TAG, "Response too large");
    return;
  }

  auto *answer = reinterpret_cast<DNSAnswer *>(this->buffer_ + answer_offset);
  answer->ptr_offset = htons(0xC000 | sizeof(DNSHeader));
  answer->type = htons(DNS_QTYPE_A);
  answer->dns_class = htons(DNS_QCLASS_IN);
  answer->ttl = htonl(DNS_ANSWER_TTL);
  answer->addr_len = htons(4);
  ip4_addr_t addr = this->server_ip_;
  answer->ip_addr = addr.addr;

  size_t response_len = answer_offset + sizeof(DNSAnswer);
  if (this->socket_->sendto(this->buffer_, response_len, 0, reinterpret_cast<struct sockaddr *>(&client_addr),
                            client_addr_len) < 0) {
    ESP_LOGV(TAG, "Send failed: %d", errno);
  }
}

}  // namespace setup_portal
}  // namespace esphome

#endif
