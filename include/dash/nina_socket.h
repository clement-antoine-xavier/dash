// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#ifndef DASHNET_DASH_NINA_SOCKET_H_
#define DASHNET_DASH_NINA_SOCKET_H_

#include <stdint.h>

#include "dash/nina_commands.h"
#include "dash/nina_frame.h"
#include "dash/types.h"

namespace dash {

// One BSD socket on the NINA module (the firmware provides five).
class NinaSocket {
 public:
  static constexpr uint8_t kNoSocket = 0xFF;
  static constexpr uint16_t kMaxRead = 1024;
  static constexpr uint16_t kMaxWrite = 1024;

  explicit NinaSocket(NinaFrame& frame) : frame_(frame) {}
  NinaSocket(NinaFrame& frame, uint8_t handle)
      : frame_(frame), socket_(handle) {}

  uint8_t Handle() const { return socket_; }
  bool IsOpen() const { return socket_ != kNoSocket; }

  bool Connect(const Ipv4& ip, uint16_t port, uint32_t timeout_ms = 10000);
  bool ConnectTls(const char* host, uint16_t port, uint32_t timeout_ms = 20000);
  bool ConnectUdp(const Ipv4& ip, uint16_t port, uint32_t timeout_ms = 3000);
  bool Listen(uint16_t port, nina::ProtMode prot_mode = nina::ProtMode::kTcp,
              uint32_t timeout_ms = 3000);

  bool Close(uint32_t timeout_ms = 2000);
  bool StopServer(uint32_t timeout_ms = 2000);
  nina::SocketState State(uint32_t timeout_ms = 1000);
  nina::SocketState ServerState(uint32_t timeout_ms = 1000);
  uint8_t Accept(uint32_t timeout_ms = 1000);
  bool Connected();
  int Available(uint32_t timeout_ms = 1000);
  int Read(void* buffer, uint16_t len, uint32_t timeout_ms = 2000);
  int Write(const void* data, uint16_t len, uint32_t timeout_ms = 5000);
  int Peek(uint32_t timeout_ms = 1000);
  bool Remote(Ipv4& ip, uint16_t& port, uint32_t timeout_ms = 2000);
  bool WaitDataSent(uint32_t timeout_ms = 2500);

  // UDP datagram primitives: insert bytes into the module buffer, then send.
  bool Insert(const void* data, uint16_t len, uint32_t timeout_ms = 3000);
  bool SendUdp(uint32_t timeout_ms = 3000);

  // Takes ownership of a socket index handed out by TcpServer.
  void Adopt(uint8_t handle);

 private:
  bool Start(const char* host, uint8_t host_len, const Ipv4& ip, uint16_t port,
             nina::ProtMode prot_mode, uint32_t timeout_ms);
  bool WaitEstablished(uint32_t timeout_ms);
  bool Stop(uint32_t timeout_ms);
  uint8_t AcquireSocket(uint32_t timeout_ms);
  int SendChunk(const uint8_t* data, uint16_t len, uint32_t timeout_ms);

  NinaFrame& frame_;
  uint8_t socket_ = kNoSocket;
};

}  // namespace dash

#endif  // DASHNET_DASH_NINA_SOCKET_H_
