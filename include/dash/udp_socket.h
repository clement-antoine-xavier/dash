// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#ifndef DASHNET_DASH_UDP_SOCKET_H_
#define DASHNET_DASH_UDP_SOCKET_H_

#include <stddef.h>
#include <stdint.h>

#include "dash/nina_socket.h"
#include "dash/types.h"

namespace dash {

class Net;

// UDP socket. Datagrams are assembled in the module's buffer and sent with
// EndPacket(); incoming datagrams are consumed with ParsePacket() and Read().
class UdpSocket {
 public:
  explicit UdpSocket(Net& net);
  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  // Listens for datagrams on a local port.
  bool Begin(uint16_t port);

  // Starts a packet towards a peer. This reconfigures the socket, so call it
  // again whenever the destination changes.
  bool BeginPacket(const Ipv4& ip, uint16_t port);
  bool BeginPacket(const char* host, uint16_t port);

  // Appends bytes to the datagram being assembled.
  int Write(const void* data, size_t len);

  // Sends the assembled datagram.
  bool EndPacket();

  // Returns the size of the next datagram, or 0 when none is pending.
  int ParsePacket();

  // Reads bytes from the current datagram, up to the number announced by
  // ParsePacket().
  int Read(void* buffer, size_t len);

  // Bytes left in the current datagram.
  int Available() const { return parsed_; }

  // Returns the next byte without consuming it, or -1.
  int Peek();

  // Remote address and port of the last datagram.
  bool Remote(Ipv4& ip, uint16_t& port);

  // Closes the socket and releases the module slot.
  void Close();

  bool IsOpen() const { return socket_.IsOpen(); }
  uint8_t Handle() const { return socket_.Handle(); }

 private:
  Net& net_;
  NinaSocket socket_;
  int parsed_ = 0;
};

}  // namespace dash

#endif  // DASHNET_DASH_UDP_SOCKET_H_
