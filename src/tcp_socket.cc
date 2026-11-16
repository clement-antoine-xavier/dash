// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#include "dash/tcp_socket.h"

#include "dash/dash_net.h"

namespace dash {

TcpSocket::TcpSocket(Net& net) : net_(net), socket_(net.Frame()) {}

bool TcpSocket::Connect(const Ipv4& ip, uint16_t port, uint32_t timeout_ms) {
  return socket_.Connect(ip, port, timeout_ms);
}

bool TcpSocket::Connect(const char* host, uint16_t port, uint32_t timeout_ms) {
  Ipv4 ip;
  if (!net_.Resolve(host, ip)) {
    return false;
  }
  return socket_.Connect(ip, port, timeout_ms);
}

bool TcpSocket::ConnectTls(const char* host, uint16_t port,
                           uint32_t timeout_ms) {
  return socket_.ConnectTls(host, port, timeout_ms);
}

int TcpSocket::Write(const void* data, size_t len) {
  if (data == nullptr || len == 0) {
    return 0;
  }
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  int total = 0;
  size_t remaining = len;
  while (remaining > 0) {
    const uint16_t chunk =
        remaining > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(remaining);
    const int written = socket_.Write(bytes + total, chunk);
    if (written <= 0) {
      break;
    }
    total += written;
    remaining -= static_cast<size_t>(written);
  }
  return total;
}

int TcpSocket::Read(void* buffer, size_t len) {
  if (buffer == nullptr || len == 0) {
    return 0;
  }
  uint8_t* bytes = static_cast<uint8_t*>(buffer);
  int total = 0;
  while (static_cast<size_t>(total) < len) {
    const size_t remaining = len - static_cast<size_t>(total);
    const uint16_t chunk =
        remaining > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(remaining);
    const int got = socket_.Read(bytes + total, chunk);
    if (got <= 0) {
      break;
    }
    total += got;
    if (static_cast<uint16_t>(got) < chunk) {
      break;  // short read: whatever the module had buffered is consumed
    }
  }
  return total;
}

bool TcpSocket::Adopt(uint8_t handle) {
  if (handle == NinaSocket::kNoSocket) {
    return false;
  }
  socket_.Adopt(handle);
  return true;
}

}  // namespace dash
