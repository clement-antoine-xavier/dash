// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#ifndef DASHNET_DASH_TCP_SOCKET_H_
#define DASHNET_DASH_TCP_SOCKET_H_

#include <stddef.h>
#include <stdint.h>

#include "dash/nina_socket.h"
#include "dash/types.h"

namespace dash {

class Net;

// Blocking TCP client. TLS runs inside the NINA module (TLS_MODE), with
// server name indication and certificate chain validation against the
// module's built-in CA bundle.
class TcpSocket {
 public:
  // Binds this socket to a Net instance. No connection is opened yet.
  explicit TcpSocket(Net& net);
  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;

  // Opens a TCP connection to an address or to a name resolved with DNS.
  // Returns false on a failed or timed out connection.
  bool Connect(const Ipv4& ip, uint16_t port, uint32_t timeout_ms = 10000);
  bool Connect(const char* host, uint16_t port, uint32_t timeout_ms = 10000);

  // Opens a TLS connection. The module performs the handshake and validates
  // the server certificate against its CA bundle.
  bool ConnectTls(const char* host, uint16_t port, uint32_t timeout_ms = 20000);

  // Writes bytes to the connection. Returns the number accepted by the
  // module, or -1 on error.
  int Write(const void* data, size_t len);

  // Reads bytes into the buffer. Returns the number copied, 0 when nothing is
  // buffered, or -1 on a socket error.
  int Read(void* buffer, size_t len);

  // Number of bytes the module holds for this socket.
  int Available() { return socket_.Available(); }

  // Returns the next byte without consuming it, or -1 when none is buffered.
  int Peek() { return socket_.Peek(); }

  // True while the connection is established or still has unread data.
  bool Connected() { return socket_.Connected(); }

  // Raw module socket state (TCP state machine).
  nina::SocketState State() { return socket_.State(); }

  // Remote address and port of the peer.
  bool Remote(Ipv4& ip, uint16_t& port) { return socket_.Remote(ip, port); }

  // Waits until the module confirms that the peer acknowledged the last
  // Write().
  bool Flush(uint32_t timeout_ms = 2500) {
    return socket_.WaitDataSent(timeout_ms);
  }

  // Closes the socket and releases the module slot.
  void Close() { socket_.Close(); }

  bool IsOpen() const { return socket_.IsOpen(); }
  uint8_t Handle() const { return socket_.Handle(); }

  // Takes over a socket index handed out by TcpServer::Accept(). Public so
  // TcpServer can bind the connection without making this a friend.
  bool Adopt(uint8_t handle);

 private:
  Net& net_;
  NinaSocket socket_;
};

}  // namespace dash

#endif  // DASHNET_DASH_TCP_SOCKET_H_
