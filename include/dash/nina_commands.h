// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT
//
// NINA SPI protocol constants and command opcodes.
//
// The NINA-W102 module runs a firmware that exposes a byte-oriented command
// protocol over SPI. The opcode numbers and framing rules below are interface
// facts of that firmware. This header is the single source of truth for them.

#ifndef DASHNET_DASH_NINA_COMMANDS_H_
#define DASHNET_DASH_NINA_COMMANDS_H_

#include <stdint.h>

namespace dash {
namespace nina {

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------
constexpr uint8_t kStartCmd = 0xE0;   // start of frame
constexpr uint8_t kEndCmd = 0xEE;     // end of frame
constexpr uint8_t kErrCmd = 0xEF;     // module reports a protocol error
constexpr uint8_t kReplyFlag = 0x80;  // or-ed into the opcode of a reply
constexpr uint8_t kDataFlag = 0x40;   // command carries 16-bit param lengths
constexpr uint8_t kDummy = 0xFF;      // idle byte / padding byte

// Reply payload of the module: 1 = ACK, 0xFF = failure.
constexpr uint8_t kAck = 0x01;
constexpr uint8_t kFail = 0xFF;

// ---------------------------------------------------------------------------
// Command opcodes
// ---------------------------------------------------------------------------
enum class Opcode : uint8_t {
  kSetNet = 0x10,
  kSetPassphrase = 0x11,
  kSetKey = 0x12,
  kSetIpConfig = 0x14,
  kSetDnsConfig = 0x15,
  kSetHostname = 0x16,
  kSetPowerMode = 0x17,
  kSetApNet = 0x18,
  kSetApPassphrase = 0x19,
  kSetDebug = 0x1A,
  kGetTemperature = 0x1B,
  kGetDnsConfig = 0x1E,
  kGetReasonCode = 0x1F,

  kGetConnStatus = 0x20,
  kGetIpAddr = 0x21,
  kGetMacAddr = 0x22,
  kGetCurrSsid = 0x23,
  kGetCurrBssid = 0x24,
  kGetCurrRssi = 0x25,
  kGetCurrEnct = 0x26,
  kScanNetworks = 0x27,
  kStartServerTcp = 0x28,
  kGetStateTcp = 0x29,
  kDataSentTcp = 0x2A,
  kAvailDataTcp = 0x2B,
  kGetDataTcp = 0x2C,
  kStartClientTcp = 0x2D,
  kStopClientTcp = 0x2E,
  kGetClientStateTcp = 0x2F,
  kDisconnect = 0x30,
  kGetIdxRssi = 0x32,
  kGetIdxEnct = 0x33,
  kReqHostByName = 0x34,
  kGetHostByName = 0x35,
  kStartScanNetworks = 0x36,
  kGetFwVersion = 0x37,
  kStopServerTcp = 0x38,
  kSendDataUdp = 0x39,
  kGetRemoteData = 0x3A,
  kGetTime = 0x3B,
  kGetIdxBssid = 0x3C,
  kGetIdxChannel = 0x3D,
  kPing = 0x3E,
  kGetSocket = 0x3F,

  kSetEnterprise = 0x40,  // 16-bit param lengths

  kSendDataTcp = 0x44,    // 16-bit param lengths
  kGetDatabufTcp = 0x45,  // 16-bit param lengths
  kInsertDatabuf = 0x46,  // 16-bit param lengths

  kBleBegin = 0x4A,
  kBleEnd = 0x4B,
  kBleAvailable = 0x4C,
  kBlePeek = 0x4D,
  kBleRead = 0x4E,
  kBleWrite = 0x4F,

  kSetPinMode = 0x50,
  kSetDigitalWrite = 0x51,
  kSetAnalogWrite = 0x52,
  kGetDigitalRead = 0x53,
  kGetAnalogRead = 0x54,

  kPreferencesBegin = 0x55,
  kPreferencesEnd = 0x56,
  kPreferencesClear = 0x57,
  kPreferencesRemove = 0x58,
  kPreferencesLen = 0x59,
  kPreferencesStat = 0x5A,
  kPreferencesPut = 0x5B,
  kPreferencesGet = 0x5C,
  kPreferencesGetType = 0x5D,

  kGetFwVersionU32 = 0x5E,
  kSetTime = 0x5F,

  kWriteFile = 0x60,
  kReadFile = 0x61,
  kDeleteFile = 0x62,
  kExistsFile = 0x63,
  kDownloadFile = 0x64,
  kApplyOta = 0x65,
  kRenameFile = 0x66,
  kDownloadOta = 0x67,
  kBrsslSetEcTa = 0x68,
  kBrsslErrorCode = 0x69,

  // 0x70..0x7F: low level BSD-like sockets on newer firmware.
};

// A handful of replies use 16-bit parameter lengths although the command does
// not necessarily carry the DATA_FLAG.
constexpr bool ReplyUsesLen16(Opcode cmd) {
  return cmd == Opcode::kGetDatabufTcp || cmd == Opcode::kPreferencesGet ||
         cmd == Opcode::kBleRead;
}

// ---------------------------------------------------------------------------
// Limits
// ---------------------------------------------------------------------------
constexpr uint8_t kSsidMaxLength = 32;
constexpr uint8_t kPassphraseMaxLength = 63;
constexpr uint8_t kWepKeyMaxLength = 13;
constexpr uint8_t kMacLength = 6;
constexpr uint8_t kIpv4Length = 4;
constexpr uint8_t kNetworksListMax = 10;
constexpr uint8_t kMaxSockets = 5;

// ---------------------------------------------------------------------------
// Socket states (wl_tcp_state)
// ---------------------------------------------------------------------------
enum class SocketState : uint8_t {
  kClosed = 0,
  kListen = 1,
  kSynSent = 2,
  kSynRcvd = 3,
  kEstablished = 4,
  kFinWait1 = 5,
  kFinWait2 = 6,
  kCloseWait = 7,
  kClosing = 8,
  kLastAck = 9,
  kTimeWait = 10,
  kUnknown = 0xFF,
};

// Converts a raw module state byte into the enumerator.
constexpr SocketState ToSocketState(uint8_t value) {
  return value <= static_cast<uint8_t>(SocketState::kTimeWait)
             ? static_cast<SocketState>(value)
             : SocketState::kUnknown;
}

// Protocol modes passed to Opcode::kStartClientTcp and Opcode::kStartServerTcp.
enum class ProtMode : uint8_t {
  kTcp = 0,
  kUdp = 1,
  kTls = 2,
  kUdpMulticast = 3,
  kTlsBearSsl = 4,
};

}  // namespace nina
}  // namespace dash

#endif  // DASHNET_DASH_NINA_COMMANDS_H_
