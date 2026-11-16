// DashNet test support - a NINA module simulator.
// SPDX-License-Identifier: MIT

#include "sim/fake_nina.h"

#include "dash/nina_commands.h"

namespace dashtest {
namespace {

constexpr uint8_t kStart = dash::nina::kStartCmd;
constexpr uint8_t kEnd = dash::nina::kEndCmd;
constexpr uint8_t kErr = dash::nina::kErrCmd;
constexpr uint8_t kDummy = dash::nina::kDummy;
constexpr uint8_t kReplyFlag = dash::nina::kReplyFlag;
constexpr uint8_t kDataFlag = dash::nina::kDataFlag;

uint8_t Key(dash::nina::Opcode cmd) { return static_cast<uint8_t>(cmd); }

}  // namespace

bool FakeNina::Begin() {
  begun_ = true;
  ready_ = true;
  ack_ = false;
  selected_ = false;
  reply_phase_ = false;
  return true;
}

void FakeNina::End() {
  begun_ = false;
  selected_ = false;
}

bool FakeNina::WaitReady(uint32_t timeout_ms) {
  const uint32_t start = Millis();
  while (!ready_) {
    if (static_cast<uint32_t>(Millis() - start) >= timeout_ms) {
      return false;
    }
  }
  return true;
}

bool FakeNina::Select(uint32_t /*timeout_ms*/) {
  if (select_fails_) {
    return false;
  }
  selected_ = true;
  ack_ = true;
  return true;
}

void FakeNina::Deselect() {
  if (!selected_) {
    return;
  }
  selected_ = false;
  ack_ = false;

  if (reply_phase_) {
    reply_consumed_ = (out_pos_ == out_.size());
    reply_phase_ = false;
    out_.clear();
    out_pos_ = 0;
    return;
  }

  tx_frames_.push_back(in_);
  Parse();
  in_.clear();
}

uint8_t FakeNina::Transfer(uint8_t data) {
  if (!selected_) {
    return kDummy;
  }
  if (reply_phase_) {
    if (out_pos_ < out_.size()) {
      return out_[out_pos_++];
    }
    // Host clocked more bytes than the reply contained.
    return kDummy;
  }
  in_.push_back(data);
  return kDummy;
}

bool FakeNina::DataReady() { return data_ready_; }

uint32_t FakeNina::Millis() { return ++now_; }

void FakeNina::DelayMs(uint32_t ms) { now_ += ms; }

void FakeNina::SetReply(dash::nina::Opcode cmd,
                        const std::vector<std::vector<uint8_t>>& params) {
  replies_[Key(cmd)] = params;
  errors_.erase(Key(cmd));
  handlers_.erase(Key(cmd));
}

void FakeNina::SetStatusReply(dash::nina::Opcode cmd, uint8_t status) {
  SetReply(cmd, {{status}});
}

void FakeNina::SetErrorReply(dash::nina::Opcode cmd) {
  errors_[Key(cmd)] = true;
  replies_.erase(Key(cmd));
  handlers_.erase(Key(cmd));
}

void FakeNina::SetHandler(dash::nina::Opcode cmd, ReplyFn fn) {
  handlers_[Key(cmd)] = fn;
  replies_.erase(Key(cmd));
  errors_.erase(Key(cmd));
}

void FakeNina::QueueError() {
  out_.clear();
  out_.push_back(kErr);
  out_pos_ = 0;
  reply_phase_ = true;
}

void FakeNina::QueueParams(dash::nina::Opcode cmd,
                           const std::vector<std::vector<uint8_t>>& params) {
  out_.clear();
  out_.push_back(kStart);
  out_.push_back(static_cast<uint8_t>(Key(cmd) | kReplyFlag));
  out_.push_back(static_cast<uint8_t>(params.size()));

  const bool len16 = dash::nina::ReplyUsesLen16(cmd);
  for (size_t p = 0; p < params.size(); ++p) {
    const std::vector<uint8_t>& param = params[p];
    if (len16) {
      out_.push_back(static_cast<uint8_t>((param.size() >> 8) & 0xFF));
    }
    out_.push_back(static_cast<uint8_t>(param.size() & 0xFF));
    out_.insert(out_.end(), param.begin(), param.end());
  }

  out_.push_back(kEnd);
  out_pos_ = 0;
  reply_phase_ = true;
  reply_consumed_ = false;
}

void FakeNina::QueueReply(dash::nina::Opcode cmd) {
  if (errors_.find(Key(cmd)) != errors_.end()) {
    QueueError();
    return;
  }
  auto it = replies_.find(Key(cmd));
  if (it != replies_.end()) {
    QueueParams(cmd, it->second);
    return;
  }
  auto handler = handlers_.find(Key(cmd));
  if (handler != handlers_.end()) {
    QueueParams(cmd, handler->second(frames_.back()));
    return;
  }
  QueueParams(cmd, {{dash::nina::kFail}});
}

void FakeNina::Parse() {
  last_parse_ok_ = false;

  if (in_.size() < 4 || in_[0] != kStart) {
    QueueError();
    return;
  }

  Frame frame;
  const uint8_t raw_cmd = in_[1];
  frame.cmd = static_cast<dash::nina::Opcode>(raw_cmd);
  const uint8_t num_params = in_[2];
  const bool len16 = (raw_cmd & kDataFlag) != 0;

  size_t i = 3;
  for (uint8_t p = 0; p < num_params; ++p) {
    size_t len = 0;
    if (len16) {
      if (i + 2 > in_.size()) {
        QueueError();
        return;
      }
      len = (static_cast<size_t>(in_[i]) << 8) | in_[i + 1];
      i += 2;
    } else {
      if (i + 1 > in_.size()) {
        QueueError();
        return;
      }
      len = in_[i];
      i += 1;
    }
    if (i + len > in_.size()) {
      QueueError();
      return;
    }
    frame.params.emplace_back(in_.data() + i, in_.data() + i + len);
    i += len;
  }

  if (i >= in_.size() || in_[i] != kEnd) {
    QueueError();
    return;
  }

  // Everything after END must be dummy padding and the whole frame must be a
  // multiple of four bytes.
  for (size_t k = i + 1; k < in_.size(); ++k) {
    if (in_[k] != kDummy) {
      QueueError();
      return;
    }
  }
  if ((in_.size() % 4) != 0) {
    QueueError();
    return;
  }

  last_parse_ok_ = true;
  frames_.push_back(frame);
  QueueReply(frame.cmd);
}

}  // namespace dashtest
