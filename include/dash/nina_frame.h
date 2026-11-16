// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#ifndef DASHNET_DASH_NINA_FRAME_H_
#define DASHNET_DASH_NINA_FRAME_H_

#include <stddef.h>
#include <stdint.h>

#include "dash/nina_commands.h"
#include "dash/nina_link.h"

namespace dash {

enum class Result : uint8_t {
  kOk = 0,
  kTimeout,        // module did not become ready / did not acknowledge
  kBadReply,       // unexpected byte, opcode, length or terminator
  kErrorCmd,       // module replied with the ERR marker (0xEF)
  kParamOverflow,  // reply parameter does not fit the caller's buffer
};

// Encodes and decodes the NINA byte protocol on top of a NinaLink.
//
// A transaction is:
//   BeginCommand(cmd, n)  ->  PutParam*() ...  ->  FinishCommand()
//   BeginReply(cmd)       ->  ReadParam*() ... ->  FinishReply()
//
// The command and its reply are two separate chip-select windows. After the
// command is sent the module raises its ACK line until the reply is ready.
class NinaFrame {
 public:
  static constexpr uint32_t kDefaultTimeoutMs = 1000;
  static constexpr uint32_t kSelectTimeoutMs = 5;

  explicit NinaFrame(NinaLink& link) : link_(link) {}

  // ---- command side -------------------------------------------------------
  Result BeginCommand(nina::Opcode cmd, uint8_t num_params,
                      uint32_t timeout_ms = kDefaultTimeoutMs);

  // 8-bit length parameter.
  void PutParam8(const void* data, uint8_t len);
  void PutParamU8(uint8_t value) { PutParam8(&value, 1); }
  void PutParamU16(uint16_t value);  // big endian
  void PutParamU32(uint32_t value);  // big endian

  // 16-bit length parameter, for commands carrying kDataFlag.
  void PutBuffer16(const void* data, uint16_t len);

  // Writes END, pads the frame to a multiple of four bytes, releases chip
  // select and waits for the module to finish preparing the reply.
  Result FinishCommand(uint32_t timeout_ms = kDefaultTimeoutMs);

  // Convenience for commands without parameters.
  Result RunCommand(nina::Opcode cmd, uint8_t num_params = 0,
                    uint32_t timeout_ms = kDefaultTimeoutMs);

  // ---- reply side ---------------------------------------------------------
  Result BeginReply(nina::Opcode cmd, uint32_t timeout_ms = kDefaultTimeoutMs);
  uint8_t ReplyParamCount() const { return reply_num_params_; }
  uint8_t ReplyParamsRead() const { return reply_index_; }

  Result ReadParam8(void* out, uint8_t cap, uint8_t* len = nullptr);
  Result ReadParam16(void* out, uint16_t cap, uint16_t* len = nullptr);

  // Consumes any unread parameters and checks the END marker.
  Result FinishReply();

  // Releases chip select and forgets any half-finished transaction.
  void Abort();

  bool IsCommandOpen() const { return command_open_; }
  bool IsReplyOpen() const { return reply_open_; }

  NinaLink& Link() { return link_; }

 private:
  Result WaitForStart();
  void WriteByte(uint8_t value);
  uint8_t ReadByte();
  void SkipParam();

  NinaLink& link_;
  bool command_open_ = false;
  bool reply_open_ = false;
  uint16_t tx_bytes_ = 0;
  bool reply_len16_ = false;
  uint8_t reply_num_params_ = 0;
  uint8_t reply_index_ = 0;
};

}  // namespace dash

#endif  // DASHNET_DASH_NINA_FRAME_H_
