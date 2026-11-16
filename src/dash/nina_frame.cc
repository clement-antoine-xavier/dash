// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#include "dash/nina_frame.h"

namespace dash {
namespace {

// A reply starts within a few bytes of chip select. This is a safety bound so
// a missing module can never spin forever.
constexpr uint8_t kStartScanLimit = 64;

// Upper bound when discarding an over-long parameter. The largest legitimate
// parameter is a socket read, which the caller sizes.
constexpr uint16_t kDiscardLimit = 4096;

}  // namespace

void NinaFrame::WriteByte(uint8_t value) { link_.Transfer(value); }

uint8_t NinaFrame::ReadByte() { return link_.Transfer(nina::kDummy); }

Result NinaFrame::WaitForStart() {
  for (uint8_t i = 0; i < kStartScanLimit; ++i) {
    uint8_t b = ReadByte();
    if (b == nina::kStartCmd) {
      return Result::kOk;
    }
    if (b == nina::kErrCmd) {
      return Result::kErrorCmd;
    }
  }
  return Result::kBadReply;
}

Result NinaFrame::BeginCommand(nina::Opcode cmd, uint8_t num_params,
                               uint32_t timeout_ms) {
  if (command_open_ || reply_open_) {
    Abort();
  }
  if (!link_.WaitReady(timeout_ms)) {
    return Result::kTimeout;
  }
  if (!link_.Select(kSelectTimeoutMs)) {
    link_.Deselect();
    return Result::kTimeout;
  }
  WriteByte(nina::kStartCmd);
  WriteByte(static_cast<uint8_t>(cmd));
  WriteByte(num_params);
  tx_bytes_ = 3;
  command_open_ = true;
  return Result::kOk;
}

void NinaFrame::PutParam8(const void* data, uint8_t len) {
  if (!command_open_) {
    return;
  }
  const uint8_t* p = static_cast<const uint8_t*>(data);
  WriteByte(len);
  for (uint8_t i = 0; i < len; ++i) {
    WriteByte(p[i]);
  }
  tx_bytes_ = static_cast<uint16_t>(tx_bytes_ + 1 + len);
}

void NinaFrame::PutParamU16(uint16_t value) {
  uint8_t buffer[2] = {static_cast<uint8_t>(value >> 8),
                       static_cast<uint8_t>(value & 0xFF)};
  PutParam8(buffer, 2);
}

void NinaFrame::PutParamU32(uint32_t value) {
  uint8_t buffer[4] = {
      static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
      static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
  PutParam8(buffer, 4);
}

void NinaFrame::PutBuffer16(const void* data, uint16_t len) {
  if (!command_open_) {
    return;
  }
  const uint8_t* p = static_cast<const uint8_t*>(data);
  WriteByte(static_cast<uint8_t>(len >> 8));
  WriteByte(static_cast<uint8_t>(len & 0xFF));
  for (uint16_t i = 0; i < len; ++i) {
    WriteByte(p[i]);
  }
  tx_bytes_ = static_cast<uint16_t>(tx_bytes_ + 2 + len);
}

Result NinaFrame::FinishCommand(uint32_t timeout_ms) {
  if (!command_open_) {
    return Result::kBadReply;
  }
  WriteByte(nina::kEndCmd);
  ++tx_bytes_;

  // The module expects every command frame to be a multiple of four bytes.
  while ((tx_bytes_ % 4) != 0) {
    WriteByte(nina::kDummy);
    ++tx_bytes_;
  }

  link_.Deselect();
  command_open_ = false;

  if (!link_.WaitReady(timeout_ms)) {
    return Result::kTimeout;
  }
  return Result::kOk;
}

Result NinaFrame::RunCommand(nina::Opcode cmd, uint8_t num_params,
                             uint32_t timeout_ms) {
  Result r = BeginCommand(cmd, num_params, timeout_ms);
  if (r != Result::kOk) {
    return r;
  }
  return FinishCommand(timeout_ms);
}

Result NinaFrame::BeginReply(nina::Opcode cmd, uint32_t timeout_ms) {
  if (command_open_ || reply_open_) {
    Abort();
  }
  if (!link_.WaitReady(timeout_ms)) {
    return Result::kTimeout;
  }
  if (!link_.Select(kSelectTimeoutMs)) {
    link_.Deselect();
    return Result::kTimeout;
  }

  Result r = WaitForStart();
  if (r != Result::kOk) {
    link_.Deselect();
    return r;
  }

  uint8_t echoed = ReadByte();
  if (echoed != (static_cast<uint8_t>(cmd) | nina::kReplyFlag)) {
    link_.Deselect();
    return Result::kBadReply;
  }

  reply_num_params_ = ReadByte();
  reply_index_ = 0;
  reply_len16_ = nina::ReplyUsesLen16(cmd);
  reply_open_ = true;
  return Result::kOk;
}

Result NinaFrame::ReadParam8(void* out, uint8_t cap, uint8_t* len) {
  if (!reply_open_ || reply_len16_ || reply_index_ >= reply_num_params_) {
    Abort();
    return Result::kBadReply;
  }

  uint8_t n = ReadByte();
  if (len != nullptr) {
    *len = n;
  }

  if (n > cap) {
    for (uint8_t i = 0; i < n; ++i) {
      ReadByte();
    }
    ++reply_index_;
    Abort();
    return Result::kParamOverflow;
  }

  uint8_t* p = static_cast<uint8_t*>(out);
  for (uint8_t i = 0; i < n; ++i) {
    p[i] = ReadByte();
  }
  ++reply_index_;
  return Result::kOk;
}

Result NinaFrame::ReadParam16(void* out, uint16_t cap, uint16_t* len) {
  if (!reply_open_ || !reply_len16_ || reply_index_ >= reply_num_params_) {
    Abort();
    return Result::kBadReply;
  }

  uint16_t n = static_cast<uint16_t>(ReadByte() << 8);
  n = static_cast<uint16_t>(n | ReadByte());
  if (len != nullptr) {
    *len = n;
  }

  if (n > cap) {
    for (uint16_t i = 0; i < n && i < kDiscardLimit; ++i) {
      ReadByte();
    }
    ++reply_index_;
    Abort();
    return Result::kParamOverflow;
  }

  uint8_t* p = static_cast<uint8_t*>(out);
  for (uint16_t i = 0; i < n; ++i) {
    p[i] = ReadByte();
  }
  ++reply_index_;
  return Result::kOk;
}

void NinaFrame::SkipParam() {
  uint16_t n = 0;
  if (reply_len16_) {
    n = static_cast<uint16_t>(ReadByte() << 8);
    n = static_cast<uint16_t>(n | ReadByte());
  } else {
    n = ReadByte();
  }
  for (uint16_t i = 0; i < n && i < kDiscardLimit; ++i) {
    ReadByte();
  }
  if (reply_index_ < reply_num_params_) {
    ++reply_index_;
  }
}

Result NinaFrame::FinishReply() {
  if (!reply_open_) {
    return Result::kBadReply;
  }

  while (reply_index_ < reply_num_params_) {
    SkipParam();
  }

  uint8_t end = ReadByte();
  link_.Deselect();
  reply_open_ = false;
  reply_num_params_ = 0;
  reply_index_ = 0;
  return end == nina::kEndCmd ? Result::kOk : Result::kBadReply;
}

void NinaFrame::Abort() {
  if (command_open_ || reply_open_) {
    link_.Deselect();
  }
  command_open_ = false;
  reply_open_ = false;
  tx_bytes_ = 0;
  reply_num_params_ = 0;
  reply_index_ = 0;
}

}  // namespace dash
