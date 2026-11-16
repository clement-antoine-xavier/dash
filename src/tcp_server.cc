// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#include "dash/tcp_server.h"

#include "dash/dash_net.h"
#include "dash/tcp_socket.h"

namespace dash {

TcpServer::TcpServer(Net& net) : socket_(net.Frame()) {}

bool TcpServer::Begin(uint16_t port) {
  return socket_.Listen(port, nina::ProtMode::kTcp);
}

bool TcpServer::End() { return socket_.StopServer(); }

bool TcpServer::Accept(TcpSocket& client) {
  const uint8_t handle = socket_.Accept();
  if (handle == NinaSocket::kNoSocket) {
    return false;
  }
  return client.Adopt(handle);
}

}  // namespace dash
