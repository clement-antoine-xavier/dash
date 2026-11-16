// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#include "dash/udp_socket.h"

#include "dash/dash_net.h"

namespace dash {

UdpSocket::UdpSocket(Net& net) : net_(net), socket_(net.Frame()) {}

bool UdpSocket::Begin(uint16_t port) {
  parsed_ = 0;
  return socket_.Listen(port, nina::ProtMode::kUdp);
}

bool UdpSocket::BeginPacket(const Ipv4& ip, uint16_t port) {
  parsed_ = 0;
  return socket_.ConnectUdp(ip, port);
}

bool UdpSocket::BeginPacket(const char* host, uint16_t port) {
  Ipv4 ip;
  if (!net_.Resolve(host, ip)) {
    return false;
  }
  return BeginPacket(ip, port);
}

int UdpSocket::Write(const void* data, size_t len) {
  if (data == nullptr || len == 0) {
    return 0;
  }
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  size_t remaining = len;
  int total = 0;
  while (remaining > 0) {
    const uint16_t chunk =
        remaining > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(remaining);
    if (!socket_.Insert(bytes + total, chunk)) {
      break;
    }
    total += chunk;
    remaining -= chunk;
  }
  return total;
}

bool UdpSocket::EndPacket() { return socket_.SendUdp(); }

int UdpSocket::ParsePacket() {
  // Discard whatever is left of the previous datagram, so a short read does
  // not leak bytes into the next one.
  uint8_t scratch[32];
  while (parsed_ > 0) {
    const uint16_t want = parsed_ < static_cast<int>(sizeof(scratch))
                              ? static_cast<uint16_t>(parsed_)
                              : static_cast<uint16_t>(sizeof(scratch));
    const int got = socket_.Read(scratch, want);
    if (got <= 0) {
      break;
    }
    parsed_ -= got;
  }
  parsed_ = socket_.Available();
  return parsed_;
}

int UdpSocket::Read(void* buffer, size_t len) {
  if (parsed_ <= 0 || buffer == nullptr || len == 0) {
    return 0;
  }
  const uint16_t want = len < static_cast<size_t>(parsed_)
                            ? static_cast<uint16_t>(len)
                            : static_cast<uint16_t>(parsed_);
  const int got = socket_.Read(buffer, want);
  if (got > 0) {
    parsed_ -= got;
  }
  return got;
}

int UdpSocket::Peek() { return socket_.Peek(); }

bool UdpSocket::Remote(Ipv4& ip, uint16_t& port) {
  return socket_.Remote(ip, port);
}

void UdpSocket::Close() {
  parsed_ = 0;
  socket_.Close();
}

}  // namespace dash
