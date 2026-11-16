// DashNet - native unit tests for the NINA frame codec.
// SPDX-License-Identifier: MIT
//
// Build and run with: make -C test

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dash/nina_commands.h"
#include "dash/nina_frame.h"
#include "dash/types.h"
#include "sim/fake_nina.h"

namespace {

int g_failures = 0;

void PrintHex(const std::vector<uint8_t>& bytes) {
  for (size_t i = 0; i < bytes.size(); ++i) {
    std::printf("%02X ", bytes[i]);
  }
  std::printf("\n");
}

void ExpectBytes(const char* what, const std::vector<uint8_t>& actual,
                 const std::vector<uint8_t>& expected) {
  if (actual == expected) {
    return;
  }
  std::printf("FAIL %s\n  expected: ", what);
  PrintHex(expected);
  std::printf("  actual:   ");
  PrintHex(actual);
  ++g_failures;
}

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using dash::NinaFrame;
using dash::Result;
namespace nina = dash::nina;

// ---------------------------------------------------------------------------
// Command encoding
// ---------------------------------------------------------------------------

void TestStatusFrame() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);
  sim.SetStatusReply(nina::Opcode::kGetConnStatus, 3);

  CHECK(frame.BeginCommand(nina::Opcode::kGetConnStatus, 0) == Result::kOk);
  CHECK(frame.FinishCommand() == Result::kOk);
  CHECK(sim.LastParseOk());
  ExpectBytes("status command", sim.TxFrames().back(),
              {0xE0, 0x20, 0x00, 0xEE});

  CHECK(frame.BeginReply(nina::Opcode::kGetConnStatus) == Result::kOk);
  CHECK(frame.ReplyParamCount() == 1);

  uint8_t value = 0;
  uint8_t len = 0;
  CHECK(frame.ReadParam8(&value, 1, &len) == Result::kOk);
  CHECK(value == 3);
  CHECK(len == 1);
  CHECK(frame.FinishReply() == Result::kOk);
  CHECK(sim.ReplyConsumed());
}

void TestMacFramePadding() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);
  sim.SetReply(nina::Opcode::kGetMacAddr,
               {{0x11, 0x22, 0x33, 0x44, 0x55, 0x66}});

  CHECK(frame.BeginCommand(nina::Opcode::kGetMacAddr, 1) == Result::kOk);
  frame.PutParamU8(0xFF);
  CHECK(frame.FinishCommand() == Result::kOk);
  CHECK(sim.LastParseOk());

  // 6 bytes of payload + END = 6 bytes, padded to 8.
  ExpectBytes("mac command", sim.TxFrames().back(),
              {0xE0, 0x22, 0x01, 0x01, 0xFF, 0xEE, 0xFF, 0xFF});

  CHECK(frame.BeginReply(nina::Opcode::kGetMacAddr) == Result::kOk);
  uint8_t mac[6] = {0};
  uint8_t len = 0;
  CHECK(frame.ReadParam8(mac, sizeof(mac), &len) == Result::kOk);
  CHECK(len == 6);
  CHECK(std::memcmp(mac, "\x11\x22\x33\x44\x55\x66", 6) == 0);
  CHECK(frame.FinishReply() == Result::kOk);
  CHECK(sim.ReplyConsumed());
}

void TestTwoParamFrame() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);
  sim.SetStatusReply(nina::Opcode::kSetPassphrase, nina::kAck);

  const char ssid[] = "ssid";
  const char pass[] = "password123";

  CHECK(frame.BeginCommand(nina::Opcode::kSetPassphrase, 2) == Result::kOk);
  frame.PutParam8(ssid, 4);
  frame.PutParam8(pass, 11);
  CHECK(frame.FinishCommand() == Result::kOk);
  CHECK(sim.LastParseOk());

  ExpectBytes(
      "set passphrase", sim.TxFrames().back(),
      {0xE0, 0x11, 0x02, 0x04, 's', 's', 'i', 'd', 0x0B, 'p',  'a',  's',
       's',  'w',  'o',  'r',  'd', '1', '2', '3', 0xEE, 0xFF, 0xFF, 0xFF});
  CHECK(sim.TxFrames().back().size() == 24);
  CHECK(sim.Frames().back().params.size() == 2);
  CHECK(sim.Frames().back().params[0].size() == 4);
  CHECK(sim.Frames().back().params[1].size() == 11);

  CHECK(frame.BeginReply(nina::Opcode::kSetPassphrase) == Result::kOk);
  uint8_t status = 0;
  CHECK(frame.ReadParam8(&status, 1) == Result::kOk);
  CHECK(status == nina::kAck);
  CHECK(frame.FinishReply() == Result::kOk);
}

void TestBuffer16Frame() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);
  sim.SetStatusReply(nina::Opcode::kInsertDatabuf, nina::kAck);

  const uint8_t sock = 2;
  const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};

  CHECK(frame.BeginCommand(nina::Opcode::kInsertDatabuf, 2) == Result::kOk);
  frame.PutBuffer16(&sock, 1);
  frame.PutBuffer16(payload, 4);
  CHECK(frame.FinishCommand() == Result::kOk);
  CHECK(sim.LastParseOk());

  ExpectBytes("insert buffer", sim.TxFrames().back(),
              {0xE0, 0x46, 0x02, 0x00, 0x01, 0x02, 0x00, 0x04, 0x01, 0x02, 0x03,
               0x04, 0xEE, 0xFF, 0xFF, 0xFF});
  CHECK(sim.Frames().back().params[0].size() == 1);
  CHECK(sim.Frames().back().params[1].size() == 4);

  CHECK(frame.BeginReply(nina::Opcode::kInsertDatabuf) == Result::kOk);
  CHECK(frame.FinishReply() == Result::kOk);
}

void TestUintParamsBigEndian() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);

  CHECK(frame.BeginCommand(nina::Opcode::kPing, 2) == Result::kOk);
  frame.PutParamU16(0x1234);
  frame.PutParamU32(0xC0A80164);
  CHECK(frame.FinishCommand() == Result::kOk);
  CHECK(sim.LastParseOk());

  ExpectBytes(
      "big endian params", sim.TxFrames().back(),
      {0xE0, 0x3E, 0x02, 0x02, 0x12, 0x34, 0x04, 0xC0, 0xA8, 0x01, 0x64, 0xEE});

  CHECK(frame.BeginReply(nina::Opcode::kPing) == Result::kOk);
  CHECK(frame.FinishReply() == Result::kOk);
}

void TestCommandSugar() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);

  CHECK(frame.RunCommand(nina::Opcode::kGetConnStatus) == Result::kOk);
  CHECK(!frame.IsCommandOpen());
  CHECK(!frame.IsReplyOpen());
  ExpectBytes("command sugar", sim.TxFrames().back(), {0xE0, 0x20, 0x00, 0xEE});

  CHECK(frame.BeginReply(nina::Opcode::kGetConnStatus) == Result::kOk);
  CHECK(frame.FinishReply() == Result::kOk);
}

// ---------------------------------------------------------------------------
// Reply decoding
// ---------------------------------------------------------------------------

void TestReplyThreeParams() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);
  sim.SetReply(nina::Opcode::kGetIpAddr,
               {{192, 168, 1, 100}, {255, 255, 255, 0}, {192, 168, 1, 1}});

  CHECK(frame.BeginCommand(nina::Opcode::kGetIpAddr, 1) == Result::kOk);
  frame.PutParamU8(nina::kDummy);
  CHECK(frame.FinishCommand() == Result::kOk);
  CHECK(frame.BeginReply(nina::Opcode::kGetIpAddr) == Result::kOk);
  CHECK(frame.ReplyParamCount() == 3);

  uint8_t ip[4] = {0}, mask[4] = {0}, gw[4] = {0};
  uint8_t len = 0;
  CHECK(frame.ReadParam8(ip, 4, &len) == Result::kOk && len == 4);
  CHECK(frame.ReadParam8(mask, 4, &len) == Result::kOk && len == 4);
  CHECK(frame.ReadParam8(gw, 4, &len) == Result::kOk && len == 4);
  CHECK(frame.FinishReply() == Result::kOk);

  const uint8_t expected_ip[4] = {192, 168, 1, 100};
  CHECK(std::memcmp(ip, expected_ip, 4) == 0);
  CHECK(mask[0] == 255 && mask[1] == 255 && mask[2] == 255 && mask[3] == 0);
  CHECK(gw[0] == 192 && gw[1] == 168 && gw[2] == 1 && gw[3] == 1);
}

void TestReplyLen16() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);
  sim.SetReply(nina::Opcode::kGetDatabufTcp, {{'H', 'E', 'L', 'L', 'O'}});

  const uint8_t sock = 3;
  const uint8_t max_len[2] = {0x00, 0x02};

  CHECK(frame.BeginCommand(nina::Opcode::kGetDatabufTcp, 2) == Result::kOk);
  frame.PutBuffer16(&sock, 1);
  frame.PutBuffer16(max_len, 2);
  CHECK(frame.FinishCommand() == Result::kOk);
  CHECK(sim.LastParseOk());

  ExpectBytes(
      "get buffer", sim.TxFrames().back(),
      {0xE0, 0x45, 0x02, 0x00, 0x01, 0x03, 0x00, 0x02, 0x00, 0x02, 0xEE, 0xFF});

  CHECK(frame.BeginReply(nina::Opcode::kGetDatabufTcp) == Result::kOk);
  char data[8] = {0};
  uint16_t len = 0;
  CHECK(frame.ReadParam16(data, sizeof(data) - 1, &len) == Result::kOk);
  CHECK(len == 5);
  CHECK(std::memcmp(data, "HELLO", 5) == 0);
  CHECK(frame.FinishReply() == Result::kOk);

  // The 8-bit reader must refuse a 16-bit reply.
  CHECK(frame.BeginCommand(nina::Opcode::kGetDatabufTcp, 2) == Result::kOk);
  frame.PutBuffer16(&sock, 1);
  frame.PutBuffer16(max_len, 2);
  CHECK(frame.FinishCommand() == Result::kOk);
  CHECK(frame.BeginReply(nina::Opcode::kGetDatabufTcp) == Result::kOk);
  uint8_t small[4] = {0};
  CHECK(frame.ReadParam8(small, sizeof(small)) == Result::kBadReply);
  CHECK(!frame.IsReplyOpen());
}

void TestReplyZeroParams() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);
  sim.SetReply(nina::Opcode::kGetFwVersion, {});

  CHECK(frame.RunCommand(nina::Opcode::kGetFwVersion) == Result::kOk);
  CHECK(frame.BeginReply(nina::Opcode::kGetFwVersion) == Result::kOk);
  CHECK(frame.ReplyParamCount() == 0);
  CHECK(frame.FinishReply() == Result::kOk);
}

void TestParamOverflow() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);
  sim.SetReply(nina::Opcode::kGetFwVersion,
               {{'1', '.', '4', '.', '8', 0, 0, 0}});

  CHECK(frame.RunCommand(nina::Opcode::kGetFwVersion) == Result::kOk);
  CHECK(frame.BeginReply(nina::Opcode::kGetFwVersion) == Result::kOk);
  char small[4] = {0};
  CHECK(frame.ReadParam8(small, sizeof(small)) == Result::kParamOverflow);
  CHECK(!frame.IsReplyOpen());

  // The link must still be usable afterwards.
  sim.SetStatusReply(nina::Opcode::kGetConnStatus, 3);
  CHECK(frame.RunCommand(nina::Opcode::kGetConnStatus) == Result::kOk);
  CHECK(frame.BeginReply(nina::Opcode::kGetConnStatus) == Result::kOk);
  uint8_t status = 0;
  CHECK(frame.ReadParam8(&status, 1) == Result::kOk);
  CHECK(status == 3);
  CHECK(frame.FinishReply() == Result::kOk);
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

void TestErrorMarker() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);
  sim.SetErrorReply(nina::Opcode::kGetConnStatus);

  CHECK(frame.RunCommand(nina::Opcode::kGetConnStatus) == Result::kOk);
  CHECK(frame.BeginReply(nina::Opcode::kGetConnStatus) == Result::kErrorCmd);
  CHECK(!frame.IsReplyOpen());

  sim.SetStatusReply(nina::Opcode::kGetConnStatus, 3);
  CHECK(frame.RunCommand(nina::Opcode::kGetConnStatus) == Result::kOk);
  CHECK(frame.BeginReply(nina::Opcode::kGetConnStatus) == Result::kOk);
  CHECK(frame.FinishReply() == Result::kOk);
}

void TestTimeout() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);

  sim.SetReady(false);
  CHECK(frame.BeginCommand(nina::Opcode::kGetConnStatus, 0) ==
        Result::kTimeout);
  CHECK(!frame.IsCommandOpen());

  sim.SetReady(true);
  CHECK(frame.BeginCommand(nina::Opcode::kGetConnStatus, 0) == Result::kOk);
  CHECK(frame.FinishCommand() == Result::kOk);
  CHECK(frame.BeginReply(nina::Opcode::kGetConnStatus) == Result::kOk);
  CHECK(frame.FinishReply() == Result::kOk);
}

void TestSelectFailure() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);

  sim.SetSelectFails(true);
  CHECK(frame.BeginCommand(nina::Opcode::kGetConnStatus, 0) ==
        Result::kTimeout);
  CHECK(!frame.IsCommandOpen());

  sim.SetSelectFails(false);
  CHECK(frame.BeginCommand(nina::Opcode::kGetConnStatus, 0) == Result::kOk);
  CHECK(frame.FinishCommand() == Result::kOk);
  CHECK(frame.BeginReply(nina::Opcode::kGetConnStatus) == Result::kOk);
  CHECK(frame.FinishReply() == Result::kOk);
}

void TestDataReady() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  sim.SetDataReady(false);
  CHECK(!sim.DataReady());
  sim.SetDataReady(true);
  CHECK(sim.DataReady());
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

void TestAllLengthsPadded() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);

  uint8_t payload[64] = {0};
  for (int len = 0; len <= 40; ++len) {
    CHECK(frame.BeginCommand(nina::Opcode::kSetNet, 1) == Result::kOk);
    frame.PutParam8(payload, static_cast<uint8_t>(len));
    CHECK(frame.FinishCommand() == Result::kOk);
    CHECK(sim.LastParseOk());
    CHECK((sim.TxFrames().back().size() % 4) == 0);
    CHECK(sim.Frames().back().params.size() == 1);
    CHECK(sim.Frames().back().params[0].size() == static_cast<size_t>(len));

    CHECK(frame.BeginReply(nina::Opcode::kSetNet) == Result::kOk);
    CHECK(frame.FinishReply() == Result::kOk);
  }
}

void TestMultipleTransactions() {
  dashtest::FakeNina sim;
  CHECK(sim.Begin());
  NinaFrame frame(sim);
  sim.SetStatusReply(nina::Opcode::kGetConnStatus, 3);
  sim.SetReply(nina::Opcode::kGetMacAddr, {{1, 2, 3, 4, 5, 6}});

  for (int i = 0; i < 3; ++i) {
    CHECK(frame.RunCommand(nina::Opcode::kGetConnStatus) == Result::kOk);
    CHECK(frame.BeginReply(nina::Opcode::kGetConnStatus) == Result::kOk);
    uint8_t status = 0;
    CHECK(frame.ReadParam8(&status, 1) == Result::kOk);
    CHECK(status == 3);
    CHECK(frame.FinishReply() == Result::kOk);

    CHECK(frame.BeginCommand(nina::Opcode::kGetMacAddr, 1) == Result::kOk);
    frame.PutParamU8(0xFF);
    CHECK(frame.FinishCommand() == Result::kOk);
    CHECK(frame.BeginReply(nina::Opcode::kGetMacAddr) == Result::kOk);
    uint8_t mac[6] = {0};
    CHECK(frame.ReadParam8(mac, 6) == Result::kOk);
    CHECK(mac[0] == 1 && mac[5] == 6);
    CHECK(frame.FinishReply() == Result::kOk);
  }

  CHECK(sim.Transactions() == 6);
}

void TestIpv4() {
  char buffer[16];
  const dash::Ipv4 ip(192, 168, 1, 100);
  CHECK(ip.ToString(buffer, sizeof(buffer)) == 13);
  CHECK(std::strcmp(buffer, "192.168.1.100") == 0);

  const dash::Ipv4 zero;
  CHECK(zero.ToString(buffer, sizeof(buffer)) == 7);
  CHECK(std::strcmp(buffer, "0.0.0.0") == 0);

  char tiny[4];
  CHECK(ip.ToString(tiny, sizeof(tiny)) == 0);
  CHECK(tiny[0] == '\0');

  uint8_t wire[4];
  ip.ToWire(wire);
  CHECK(wire[0] == 100 && wire[1] == 1 && wire[2] == 168 && wire[3] == 192);
  CHECK(dash::Ipv4::FromWire(wire) == ip);
  CHECK(ip.ToU32() == 0xC0A80164UL);
  CHECK(!ip.IsZero());
}

}  // namespace

int main() {
  TestStatusFrame();
  TestMacFramePadding();
  TestTwoParamFrame();
  TestBuffer16Frame();
  TestUintParamsBigEndian();
  TestCommandSugar();
  TestReplyThreeParams();
  TestReplyLen16();
  TestReplyZeroParams();
  TestParamOverflow();
  TestErrorMarker();
  TestTimeout();
  TestSelectFailure();
  TestDataReady();
  TestAllLengthsPadded();
  TestMultipleTransactions();
  TestIpv4();

  if (g_failures == 0) {
    std::printf("all tests passed\n");
    return 0;
  }
  std::printf("%d check(s) failed\n", g_failures);
  return 1;
}
