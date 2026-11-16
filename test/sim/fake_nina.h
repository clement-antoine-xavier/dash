// DashNet test support - a NINA module simulator.
// SPDX-License-Identifier: MIT

#ifndef DASHNET_TEST_SIM_FAKE_NINA_H_
#define DASHNET_TEST_SIM_FAKE_NINA_H_

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <map>
#include <vector>

#include "dash/nina_commands.h"
#include "dash/nina_link.h"

namespace dashtest {

// Models the byte stream behaviour of the NINA firmware: chip-select windows,
// the ACK handshake, command frame parsing (including 4-byte padding) and
// canned replies.
class FakeNina : public dash::NinaLink {
 public:
  struct Frame {
    dash::nina::Opcode cmd = static_cast<dash::nina::Opcode>(0);
    std::vector<std::vector<uint8_t>> params;
  };

  using ReplyFn =
      std::function<std::vector<std::vector<uint8_t>>(const Frame&)>;

  // --- NinaLink -----------------------------------------------------------
  bool Begin() override;
  void End() override;
  bool WaitReady(uint32_t timeout_ms) override;
  bool Select(uint32_t timeout_ms) override;
  void Deselect() override;
  uint8_t Transfer(uint8_t data) override;
  bool DataReady() override;
  uint32_t Millis() override;
  void DelayMs(uint32_t ms) override;

  // --- configuration ------------------------------------------------------
  void SetReady(bool ready) { ready_ = ready; }
  void SetSelectFails(bool fails) { select_fails_ = fails; }
  void SetDataReady(bool ready) { data_ready_ = ready; }

  void SetReply(dash::nina::Opcode cmd,
                const std::vector<std::vector<uint8_t>>& params);
  void SetStatusReply(dash::nina::Opcode cmd, uint8_t status = 0x01);
  void SetErrorReply(dash::nina::Opcode cmd);
  // Dynamic reply, e.g. for polling a status that changes over time.
  void SetHandler(dash::nina::Opcode cmd, ReplyFn fn);

  // --- observation --------------------------------------------------------
  const std::vector<std::vector<uint8_t>>& TxFrames() const {
    return tx_frames_;
  }
  const std::vector<Frame>& Frames() const { return frames_; }
  bool LastParseOk() const { return last_parse_ok_; }
  bool ReplyConsumed() const { return reply_consumed_; }
  size_t Transactions() const { return tx_frames_.size(); }

 private:
  void Parse();
  void QueueReply(dash::nina::Opcode cmd);
  void QueueError();
  void QueueParams(dash::nina::Opcode cmd,
                   const std::vector<std::vector<uint8_t>>& params);

  bool begun_ = false;
  bool ready_ = true;
  bool ack_ = false;
  bool selected_ = false;
  bool select_fails_ = false;
  bool data_ready_ = false;
  bool reply_phase_ = false;
  bool reply_consumed_ = true;
  bool last_parse_ok_ = false;
  uint32_t now_ = 0;

  std::vector<uint8_t> in_;
  std::vector<uint8_t> out_;
  size_t out_pos_ = 0;

  std::map<uint8_t, std::vector<std::vector<uint8_t>>> replies_;
  std::map<uint8_t, bool> errors_;
  std::map<uint8_t, ReplyFn> handlers_;

  std::vector<std::vector<uint8_t>> tx_frames_;
  std::vector<Frame> frames_;
};

}  // namespace dashtest

#endif  // DASHNET_TEST_SIM_FAKE_NINA_H_
