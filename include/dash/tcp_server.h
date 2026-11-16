// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#ifndef DASHNET_DASH_TCP_SERVER_H_
#define DASHNET_DASH_TCP_SERVER_H_

#include <stdint.h>

#include "dash/nina_socket.h"

namespace dash {

class Net;
class TcpSocket;

// TCP listener. Accepted connections are bound to caller supplied TcpSocket
// objects so that every socket lifetime stays explicit.
class TcpServer {
 public:
  explicit TcpServer(Net& net);
  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  // Listens on a local TCP port. Replaces any previous listener.
  bool Begin(uint16_t port);

  // Stops listening and releases the module slot.
  bool End();

  // returns true and binds `client` when a connection was accepted.
  bool Accept(TcpSocket& client);

  // Raw module state of the listening socket.
  nina::SocketState State() { return socket_.ServerState(); }

  bool IsListening() const { return socket_.IsOpen(); }
  uint8_t Handle() const { return socket_.Handle(); }

 private:
  NinaSocket socket_;
};

}  // namespace dash

#endif  // DASHNET_DASH_TCP_SERVER_H_
