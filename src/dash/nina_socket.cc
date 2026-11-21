// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#include "dash/nina_socket.h"

#include <string.h>

namespace dash {
namespace {

// NINA firmware 1.4.x ignores the startClientTcp timeout parameter and can
// hold a connect for roughly 20 s. When the frame wait expires, keep waiting
// for that reply before any other command is sent.
constexpr uint32_t kLateReplyTimeoutMs = 30000;

bool ReadStatus(NinaFrame& frame, nina::Opcode opcode, uint32_t timeout_ms) {
  if (frame.BeginReply(opcode, timeout_ms) != Result::kOk) {
    return false;
  }
  // A failed socket operation is reported with zero parameters. Never call
  // ReadParam8 in that case: it aborts the reply and leaves the END marker
  // unread, which desynchronises every following transaction.
  bool ok = frame.ReplyParamCount() != 0;
  uint8_t status = 0;
  uint8_t len = 0;
  if (ok) {
    ok = frame.ReadParam8(&status, 1, &len) == Result::kOk && len == 1;
  }
  if (frame.FinishReply() != Result::kOk) {
    return false;
  }
  return ok && status == nina::kAck;
}

// Reads a single parameter of up to four bytes and returns it as a uint32_t
// in little endian order, matching the module's native wire format.
bool ReadU32(NinaFrame& frame, nina::Opcode opcode, uint32_t& value,
             uint32_t timeout_ms) {
  if (frame.BeginReply(opcode, timeout_ms) != Result::kOk) {
    return false;
  }
  uint8_t buffer[4] = {0};
  uint8_t len = 0;
  if (frame.ReadParam8(buffer, sizeof(buffer), &len) != Result::kOk) {
    frame.Abort();
    return false;
  }
  if (frame.FinishReply() != Result::kOk) {
    return false;
  }
  value = 0;
  for (uint8_t i = 0; i < len && i < 4; ++i) {
    value |= static_cast<uint32_t>(buffer[i]) << (8 * i);
  }
  return true;
}

bool IsClosedState(nina::SocketState state) {
  return state == nina::SocketState::kUnknown ||
         state == nina::SocketState::kClosed ||
         state == nina::SocketState::kCloseWait ||
         state == nina::SocketState::kTimeWait;
}

}  // namespace

bool NinaSocket::Connect(const Ipv4& ip, uint16_t port, uint32_t timeout_ms) {
  return Start(nullptr, 0, ip, port, nina::ProtMode::kTcp, timeout_ms);
}

bool NinaSocket::ConnectTls(const char* host, uint16_t port,
                            uint32_t timeout_ms) {
  if (host == nullptr) {
    return false;
  }
  const size_t len = strlen(host);
  if (len == 0 || len > 255) {
    return false;
  }
  return Start(host, static_cast<uint8_t>(len), Ipv4(), port,
               nina::ProtMode::kTls, timeout_ms);
}

bool NinaSocket::ConnectUdp(const Ipv4& ip, uint16_t port,
                            uint32_t timeout_ms) {
  if (socket_ != kNoSocket) {
    Stop(1000);
  }

  const uint8_t socket = AcquireSocket(timeout_ms);
  if (socket == kNoSocket) {
    return false;
  }
  socket_ = socket;

  if (frame_.BeginCommand(nina::Opcode::kStartClientTcp, 4, timeout_ms) !=
      Result::kOk) {
    ReleaseAfterFailure(500);
    return false;
  }
  uint8_t wire[4];
  ip.ToWire(wire);
  frame_.PutParam8(wire, 4);
  frame_.PutParamU16(port);
  frame_.PutParamU8(socket);
  frame_.PutParamU8(static_cast<uint8_t>(nina::ProtMode::kUdp));
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    ReleaseAfterFailure(500);
    return false;
  }
  if (!ReadStatus(frame_, nina::Opcode::kStartClientTcp, timeout_ms)) {
    ReleaseAfterFailure(500);
    return false;
  }
  return true;
}

bool NinaSocket::Listen(uint16_t port, nina::ProtMode prot_mode,
                        uint32_t timeout_ms) {
  if (socket_ != kNoSocket) {
    Stop(1000);
  }

  const uint8_t socket = AcquireSocket(timeout_ms);
  if (socket == kNoSocket) {
    return false;
  }
  socket_ = socket;

  if (frame_.BeginCommand(nina::Opcode::kStartServerTcp, 3, timeout_ms) !=
      Result::kOk) {
    ReleaseAfterFailure(500);
    return false;
  }
  frame_.PutParamU16(port);
  frame_.PutParamU8(socket);
  frame_.PutParamU8(static_cast<uint8_t>(prot_mode));
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    ReleaseAfterFailure(500);
    return false;
  }
  if (!ReadStatus(frame_, nina::Opcode::kStartServerTcp, timeout_ms)) {
    ReleaseAfterFailure(500);
    return false;
  }
  return true;
}

bool NinaSocket::Start(const char* host, uint8_t host_len, const Ipv4& ip,
                       uint16_t port, nina::ProtMode prot_mode,
                       uint32_t timeout_ms) {
  if (socket_ != kNoSocket) {
    Stop(1000);
  }

  const uint8_t socket = AcquireSocket(timeout_ms);
  if (socket == kNoSocket) {
    return false;
  }
  socket_ = socket;

  const uint32_t bounded_timeout = timeout_ms > 65535UL ? 65535UL : timeout_ms;

  if (frame_.BeginCommand(nina::Opcode::kStartClientTcp, 6, timeout_ms) !=
      Result::kOk) {
    ReleaseAfterFailure(500);
    return false;
  }
  frame_.PutParam8(host != nullptr ? host : "", host_len);

  uint8_t wire[4];
  ip.ToWire(wire);
  frame_.PutParam8(wire, 4);
  frame_.PutParamU16(port);
  frame_.PutParamU8(socket);
  frame_.PutParamU8(static_cast<uint8_t>(prot_mode));
  frame_.PutParamU16(static_cast<uint16_t>(bounded_timeout));

  // The module holds ACK high for the whole blocking connect and answers at
  // the connect timeout. Give the reply a little slack so that timeout and
  // reply do not race; otherwise the next command is sent while the previous
  // reply is still pending.
  const Result finished = frame_.FinishCommand(timeout_ms + 2000);
  if (finished != Result::kOk) {
    // Drain a late reply before touching any other socket.
    DrainPendingReply(nina::Opcode::kStartClientTcp, kLateReplyTimeoutMs);
    ReleaseAfterFailure(1000);
    return false;
  }
  if (!ReadStatus(frame_, nina::Opcode::kStartClientTcp, timeout_ms)) {
    ReleaseAfterFailure(500);
    return false;
  }
  if (!WaitEstablished(timeout_ms)) {
    ReleaseAfterFailure(1000);
    return false;
  }
  return true;
}

bool NinaSocket::WaitEstablished(uint32_t timeout_ms) {
  const uint32_t begin = frame_.Link().Millis();
  for (;;) {
    const nina::SocketState value = State();
    if (value == nina::SocketState::kEstablished) {
      return true;
    }
    if (IsClosedState(value)) {
      return false;
    }
    if (static_cast<uint32_t>(frame_.Link().Millis() - begin) >= timeout_ms) {
      return false;
    }
    frame_.Link().DelayMs(50);
  }
}

uint8_t NinaSocket::RequestSocket(uint32_t timeout_ms) {
  if (frame_.RunCommand(nina::Opcode::kGetSocket, 0, timeout_ms) !=
      Result::kOk) {
    return kNoSocket;
  }
  if (frame_.BeginReply(nina::Opcode::kGetSocket, timeout_ms) != Result::kOk) {
    return kNoSocket;
  }
  uint8_t value = kNoSocket;
  uint8_t len = 0;
  bool ok = frame_.ReplyParamCount() != 0;
  if (ok) {
    ok = frame_.ReadParam8(&value, 1, &len) == Result::kOk && len == 1;
  }
  if (frame_.FinishReply() != Result::kOk || !ok) {
    return kNoSocket;
  }
  return value;
}

uint8_t NinaSocket::AcquireSocket(uint32_t timeout_ms) {
  uint8_t value = RequestSocket(timeout_ms);
  if (value <= kMaxSocketIndex) {
    return value;
  }

  // The module's slot table can be left stale, for example after a failed
  // connect. Reset every client socket once and ask again.
  RecoverSockets(timeout_ms);
  value = RequestSocket(timeout_ms);
  return value <= kMaxSocketIndex ? value : kNoSocket;
}

bool NinaSocket::DrainPendingReply(nina::Opcode opcode, uint32_t timeout_ms) {
  if (frame_.BeginReply(opcode, timeout_ms) != Result::kOk) {
    return false;
  }
  return frame_.FinishReply() == Result::kOk;
}

void NinaSocket::RecoverSockets(uint32_t timeout_ms) {
  for (uint8_t i = 0; i <= kMaxSocketIndex; ++i) {
    if (frame_.BeginCommand(nina::Opcode::kStopClientTcp, 1, timeout_ms) !=
        Result::kOk) {
      return;
    }
    frame_.PutParamU8(i);
    if (frame_.FinishCommand(timeout_ms) == Result::kOk) {
      // Drain the reply even when the status is a failure.
      ReadStatus(frame_, nina::Opcode::kStopClientTcp, timeout_ms);
    }
  }
}

void NinaSocket::ReleaseAfterFailure(uint32_t timeout_ms) {
  if (!Stop(timeout_ms)) {
    // A failed stop can leave the slot allocated; clear the whole pool so the
    // next connect has a chance.
    RecoverSockets(timeout_ms);
  }
}

bool NinaSocket::Stop(uint32_t timeout_ms) {
  if (socket_ == kNoSocket) {
    return true;
  }
  const uint8_t socket = socket_;
  socket_ = kNoSocket;

  if (frame_.BeginCommand(nina::Opcode::kStopClientTcp, 1, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  frame_.PutParamU8(socket);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return false;
  }
  return ReadStatus(frame_, nina::Opcode::kStopClientTcp, timeout_ms);
}

bool NinaSocket::Close(uint32_t timeout_ms) { return Stop(timeout_ms); }

bool NinaSocket::StopServer(uint32_t timeout_ms) {
  if (socket_ == kNoSocket) {
    return true;
  }
  const uint8_t socket = socket_;
  socket_ = kNoSocket;

  if (frame_.BeginCommand(nina::Opcode::kStopServerTcp, 1, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  frame_.PutParamU8(socket);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return false;
  }
  return ReadStatus(frame_, nina::Opcode::kStopServerTcp, timeout_ms);
}

nina::SocketState NinaSocket::ServerState(uint32_t timeout_ms) {
  if (socket_ == kNoSocket) {
    return nina::SocketState::kClosed;
  }
  if (frame_.BeginCommand(nina::Opcode::kGetStateTcp, 1, timeout_ms) !=
      Result::kOk) {
    return nina::SocketState::kUnknown;
  }
  frame_.PutParamU8(socket_);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return nina::SocketState::kUnknown;
  }
  if (frame_.BeginReply(nina::Opcode::kGetStateTcp, timeout_ms) !=
      Result::kOk) {
    return nina::SocketState::kUnknown;
  }
  uint8_t value = 0xFF;
  uint8_t len = 0;
  if (frame_.ReadParam8(&value, 1, &len) != Result::kOk) {
    frame_.Abort();
    return nina::SocketState::kUnknown;
  }
  if (frame_.FinishReply() != Result::kOk) {
    return nina::SocketState::kUnknown;
  }
  return nina::ToSocketState(value);
}

uint8_t NinaSocket::Accept(uint32_t timeout_ms) {
  if (socket_ == kNoSocket) {
    return kNoSocket;
  }
  if (frame_.BeginCommand(nina::Opcode::kAvailDataTcp, 2, timeout_ms) !=
      Result::kOk) {
    return kNoSocket;
  }
  frame_.PutParamU8(socket_);
  frame_.PutParamU8(1);  // accept a pending connection
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return kNoSocket;
  }
  if (frame_.BeginReply(nina::Opcode::kAvailDataTcp, timeout_ms) !=
      Result::kOk) {
    return kNoSocket;
  }
  uint8_t value = kNoSocket;
  uint8_t len = 0;
  if (frame_.ReadParam8(&value, 1, &len) != Result::kOk) {
    frame_.Abort();
    return kNoSocket;
  }
  if (frame_.FinishReply() != Result::kOk) {
    return kNoSocket;
  }
  return value;
}

bool NinaSocket::Insert(const void* data, uint16_t len, uint32_t timeout_ms) {
  if (socket_ == kNoSocket || data == nullptr) {
    return false;
  }
  const uint8_t socket = socket_;
  if (frame_.BeginCommand(nina::Opcode::kInsertDatabuf, 2, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  frame_.PutBuffer16(&socket, 1);
  frame_.PutBuffer16(data, len);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return false;
  }
  return ReadStatus(frame_, nina::Opcode::kInsertDatabuf, timeout_ms);
}

bool NinaSocket::SendUdp(uint32_t timeout_ms) {
  if (socket_ == kNoSocket) {
    return false;
  }
  if (frame_.BeginCommand(nina::Opcode::kSendDataUdp, 1, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  frame_.PutParamU8(socket_);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return false;
  }
  return ReadStatus(frame_, nina::Opcode::kSendDataUdp, timeout_ms);
}

void NinaSocket::Adopt(uint8_t handle) {
  if (socket_ != kNoSocket && socket_ != handle) {
    Stop(500);
  }
  socket_ = handle;
}

nina::SocketState NinaSocket::State(uint32_t timeout_ms) {
  if (socket_ == kNoSocket) {
    return nina::SocketState::kClosed;
  }
  if (frame_.BeginCommand(nina::Opcode::kGetClientStateTcp, 1, timeout_ms) !=
      Result::kOk) {
    return nina::SocketState::kUnknown;
  }
  frame_.PutParamU8(socket_);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return nina::SocketState::kUnknown;
  }
  if (frame_.BeginReply(nina::Opcode::kGetClientStateTcp, timeout_ms) !=
      Result::kOk) {
    return nina::SocketState::kUnknown;
  }
  uint8_t value = 0xFF;
  uint8_t len = 0;
  if (frame_.ReadParam8(&value, 1, &len) != Result::kOk) {
    frame_.Abort();
    return nina::SocketState::kUnknown;
  }
  if (frame_.FinishReply() != Result::kOk) {
    return nina::SocketState::kUnknown;
  }
  return nina::ToSocketState(value);
}

bool NinaSocket::Connected() {
  if (socket_ == kNoSocket) {
    return false;
  }
  if (Available() > 0) {
    return true;
  }
  const nina::SocketState value = State();
  const bool alive = !(value == nina::SocketState::kUnknown ||
                       value == nina::SocketState::kListen ||
                       value == nina::SocketState::kClosed ||
                       value == nina::SocketState::kFinWait1 ||
                       value == nina::SocketState::kFinWait2 ||
                       value == nina::SocketState::kTimeWait ||
                       value == nina::SocketState::kSynSent ||
                       value == nina::SocketState::kSynRcvd ||
                       value == nina::SocketState::kCloseWait);
  if (!alive) {
    socket_ = kNoSocket;
  }
  return alive;
}

int NinaSocket::Available(uint32_t timeout_ms) {
  if (socket_ == kNoSocket) {
    return 0;
  }
  if (!frame_.Link().DataReady()) {
    return 0;
  }
  if (frame_.BeginCommand(nina::Opcode::kAvailDataTcp, 1, timeout_ms) !=
      Result::kOk) {
    return 0;
  }
  frame_.PutParamU8(socket_);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return 0;
  }
  uint32_t value = 0;
  if (!ReadU32(frame_, nina::Opcode::kAvailDataTcp, value, timeout_ms)) {
    return 0;
  }
  return static_cast<int>(value & 0xFFFF);
}

int NinaSocket::Read(void* buffer, uint16_t len, uint32_t timeout_ms) {
  if (socket_ == kNoSocket) {
    return -1;
  }
  if (len == 0) {
    return 0;
  }
  if (!frame_.Link().DataReady()) {
    return 0;
  }

  const uint16_t want = len > kMaxRead ? kMaxRead : len;
  const uint8_t socket = socket_;
  const uint8_t max_bytes[2] = {static_cast<uint8_t>(want & 0xFF),
                                static_cast<uint8_t>(want >> 8)};

  if (frame_.BeginCommand(nina::Opcode::kGetDatabufTcp, 2, timeout_ms) !=
      Result::kOk) {
    return -1;
  }
  frame_.PutBuffer16(&socket, 1);
  frame_.PutBuffer16(max_bytes, 2);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return -1;
  }
  if (frame_.BeginReply(nina::Opcode::kGetDatabufTcp, timeout_ms) !=
      Result::kOk) {
    return -1;
  }

  uint16_t got = 0;
  if (frame_.ReadParam16(buffer, want, &got) != Result::kOk) {
    return -1;
  }
  if (frame_.FinishReply() != Result::kOk) {
    return -1;
  }
  return static_cast<int>(got);
}

int NinaSocket::SendChunk(const uint8_t* data, uint16_t len,
                          uint32_t timeout_ms) {
  const uint8_t socket = socket_;
  if (frame_.BeginCommand(nina::Opcode::kSendDataTcp, 2, timeout_ms) !=
      Result::kOk) {
    return -1;
  }
  frame_.PutBuffer16(&socket, 1);
  frame_.PutBuffer16(data, len);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return -1;
  }

  // The module replies with the number of bytes accepted as a little endian
  // value of one or two bytes.
  uint32_t accepted = 0;
  if (!ReadU32(frame_, nina::Opcode::kSendDataTcp, accepted, timeout_ms)) {
    return -1;
  }
  if (accepted == 0) {
    return -1;
  }
  return static_cast<int>(accepted > len ? len : accepted);
}

int NinaSocket::Write(const void* data, uint16_t len, uint32_t timeout_ms) {
  if (socket_ == kNoSocket) {
    return -1;
  }
  if (data == nullptr || len == 0) {
    return 0;
  }

  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  int total = 0;
  while (total < len) {
    const uint16_t remaining = static_cast<uint16_t>(len - total);
    const uint16_t chunk = remaining > kMaxWrite ? kMaxWrite : remaining;
    const int written = SendChunk(bytes + total, chunk, timeout_ms);
    if (written <= 0) {
      break;
    }
    total += written;
  }
  return total;
}

int NinaSocket::Peek(uint32_t timeout_ms) {
  if (socket_ == kNoSocket) {
    return -1;
  }
  if (!frame_.Link().DataReady()) {
    return -1;
  }
  if (frame_.BeginCommand(nina::Opcode::kGetDataTcp, 2, timeout_ms) !=
      Result::kOk) {
    return -1;
  }
  frame_.PutParamU8(socket_);
  frame_.PutParamU8(1);  // peek, do not consume
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return -1;
  }
  if (frame_.BeginReply(nina::Opcode::kGetDataTcp, timeout_ms) != Result::kOk) {
    return -1;
  }
  uint8_t value = 0;
  uint8_t len = 0;
  if (frame_.ReadParam8(&value, 1, &len) != Result::kOk) {
    return -1;
  }
  if (frame_.FinishReply() != Result::kOk || len == 0) {
    return -1;
  }
  return value;
}

bool NinaSocket::Remote(Ipv4& ip, uint16_t& port, uint32_t timeout_ms) {
  if (socket_ == kNoSocket) {
    return false;
  }
  if (frame_.BeginCommand(nina::Opcode::kGetRemoteData, 1, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  frame_.PutParamU8(socket_);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return false;
  }
  if (frame_.BeginReply(nina::Opcode::kGetRemoteData, timeout_ms) !=
      Result::kOk) {
    return false;
  }

  uint8_t address[4] = {0};
  uint8_t port_bytes[2] = {0};
  uint8_t len = 0;
  if (frame_.ReadParam8(address, sizeof(address), &len) != Result::kOk ||
      len != 4) {
    frame_.Abort();
    return false;
  }
  if (frame_.ReadParam8(port_bytes, sizeof(port_bytes), &len) != Result::kOk ||
      len != 2) {
    frame_.Abort();
    return false;
  }
  if (frame_.FinishReply() != Result::kOk) {
    return false;
  }

  ip = Ipv4(address[0], address[1], address[2], address[3]);
  port = static_cast<uint16_t>((port_bytes[0] << 8) | port_bytes[1]);
  return true;
}

bool NinaSocket::WaitDataSent(uint32_t timeout_ms) {
  if (socket_ == kNoSocket) {
    return false;
  }
  const uint32_t begin = frame_.Link().Millis();
  for (;;) {
    if (frame_.BeginCommand(nina::Opcode::kDataSentTcp, 1, timeout_ms) ==
        Result::kOk) {
      frame_.PutParamU8(socket_);
      if (frame_.FinishCommand(timeout_ms) == Result::kOk) {
        if (frame_.BeginReply(nina::Opcode::kDataSentTcp, timeout_ms) ==
            Result::kOk) {
          uint8_t value = 0;
          uint8_t len = 0;
          if (frame_.ReadParam8(&value, 1, &len) == Result::kOk &&
              frame_.FinishReply() == Result::kOk && value != 0) {
            return true;
          }
        }
      }
    }
    if (static_cast<uint32_t>(frame_.Link().Millis() - begin) >= timeout_ms) {
      return false;
    }
    frame_.Link().DelayMs(100);
  }
}

}  // namespace dash
