// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#include "dash/nina_wifi.h"

#include <string.h>

namespace dash {
namespace {

constexpr uint8_t kConnected = 3;
constexpr uint8_t kConnectFailed = 4;
constexpr uint8_t kConnectionLost = 5;
constexpr uint32_t kPollIntervalMs = 200;

bool ReadIndexed(NinaFrame& frame, nina::Opcode opcode, uint8_t index,
                 uint8_t* out, uint8_t cap, uint8_t& len, uint32_t timeout_ms) {
  len = 0;
  if (frame.BeginCommand(opcode, 1, timeout_ms) != Result::kOk) {
    return false;
  }
  frame.PutParamU8(index);
  if (frame.FinishCommand(timeout_ms) != Result::kOk) {
    return false;
  }
  if (frame.BeginReply(opcode, timeout_ms) != Result::kOk) {
    return false;
  }
  if (frame.ReadParam8(out, cap, &len) != Result::kOk) {
    return false;
  }
  return frame.FinishReply() == Result::kOk;
}

int32_t DecodeInt32Le(const uint8_t* p) {
  return static_cast<int32_t>(static_cast<uint32_t>(p[0]) |
                              (static_cast<uint32_t>(p[1]) << 8) |
                              (static_cast<uint32_t>(p[2]) << 16) |
                              (static_cast<uint32_t>(p[3]) << 24));
}

}  // namespace

// ---------------------------------------------------------------------------
// Bring-up
// ---------------------------------------------------------------------------

bool NinaWifi::Begin(uint32_t ready_timeout_ms) {
  fw_[0] = '\0';
  if (!link_.Begin()) {
    return false;
  }
  return Identify(ready_timeout_ms);
}

bool NinaWifi::Identify(uint32_t timeout_ms) {
  uint8_t len = 0;
  if (!ReadString(nina::Opcode::kGetFwVersion, fw_, sizeof(fw_) - 1, len,
                  timeout_ms)) {
    fw_[0] = '\0';
    return false;
  }
  fw_[len] = '\0';
  return len > 0;
}

bool NinaWifi::ReadString(nina::Opcode opcode, char* out, uint8_t cap,
                          uint8_t& len, uint32_t timeout_ms) {
  len = 0;
  Result r = frame_.RunCommand(opcode, 0, timeout_ms);
  if (r != Result::kOk) {
    return false;
  }
  r = frame_.BeginReply(opcode, timeout_ms);
  if (r != Result::kOk) {
    return false;
  }
  uint8_t n = 0;
  r = frame_.ReadParam8(out, cap, &n);
  if (r != Result::kOk) {
    return false;
  }
  len = n;
  return frame_.FinishReply() == Result::kOk;
}

bool NinaWifi::ReadSingleByte(nina::Opcode opcode, uint8_t& out,
                              uint32_t timeout_ms) {
  Result r = frame_.RunCommand(opcode, 0, timeout_ms);
  if (r != Result::kOk) {
    return false;
  }
  r = frame_.BeginReply(opcode, timeout_ms);
  if (r != Result::kOk) {
    return false;
  }
  uint8_t len = 0;
  r = frame_.ReadParam8(&out, 1, &len);
  if (r != Result::kOk || len != 1) {
    frame_.Abort();
    return false;
  }
  return frame_.FinishReply() == Result::kOk;
}

bool NinaWifi::ReadStatusReply(nina::Opcode opcode, uint32_t timeout_ms) {
  Result r = frame_.BeginReply(opcode, timeout_ms);
  if (r != Result::kOk) {
    return false;
  }
  // Some commands report failure with zero parameters. Consume the END marker
  // even then, otherwise the next transaction starts mid-frame.
  bool ok = frame_.ReplyParamCount() != 0;
  uint8_t status = 0;
  uint8_t len = 0;
  if (ok) {
    ok = frame_.ReadParam8(&status, 1, &len) == Result::kOk && len == 1;
  }
  if (frame_.FinishReply() != Result::kOk) {
    return false;
  }
  return ok && status == nina::kAck;
}

bool NinaWifi::StartDummyCommand(nina::Opcode opcode, uint32_t timeout_ms) {
  Result r = frame_.BeginCommand(opcode, 1, timeout_ms);
  if (r != Result::kOk) {
    return false;
  }
  frame_.PutParamU8(nina::kDummy);
  return frame_.FinishCommand(timeout_ms) == Result::kOk;
}

bool NinaWifi::ReadInt32(nina::Opcode opcode, int32_t& out,
                         uint32_t timeout_ms) {
  if (!StartDummyCommand(opcode, timeout_ms)) {
    return false;
  }
  if (frame_.BeginReply(opcode, timeout_ms) != Result::kOk) {
    return false;
  }
  uint8_t buffer[4] = {0};
  uint8_t len = 0;
  if (frame_.ReadParam8(buffer, sizeof(buffer), &len) != Result::kOk ||
      len != 4) {
    frame_.Abort();
    return false;
  }
  if (frame_.FinishReply() != Result::kOk) {
    return false;
  }
  out = DecodeInt32Le(buffer);
  return true;
}

void NinaWifi::PutIpv4Param(const Ipv4& address) {
  uint8_t wire[4];
  address.ToWire(wire);
  frame_.PutParam8(wire, 4);
}

// ---------------------------------------------------------------------------
// Identification
// ---------------------------------------------------------------------------

uint32_t NinaWifi::FirmwareVersionU32(uint32_t timeout_ms) {
  uint8_t buffer[4] = {0, 0, 0, 0};
  uint8_t len = 0;

  Result r = frame_.RunCommand(nina::Opcode::kGetFwVersionU32, 0, timeout_ms);
  if (r == Result::kOk) {
    r = frame_.BeginReply(nina::Opcode::kGetFwVersionU32, timeout_ms);
  }
  if (r == Result::kOk) {
    r = frame_.ReadParam8(buffer, sizeof(buffer), &len);
  }
  if (r != Result::kOk) {
    frame_.Abort();
    return 0;
  }
  if (frame_.FinishReply() != Result::kOk) {
    return 0;
  }
  if (len >= 3) {
    return (static_cast<uint32_t>(buffer[0]) << 16) |
           (static_cast<uint32_t>(buffer[1]) << 8) | buffer[2];
  }
  return 0;
}

bool NinaWifi::Mac(uint8_t out[nina::kMacLength], uint32_t timeout_ms) {
  if (!StartDummyCommand(nina::Opcode::kGetMacAddr, timeout_ms)) {
    return false;
  }
  if (frame_.BeginReply(nina::Opcode::kGetMacAddr, timeout_ms) != Result::kOk) {
    return false;
  }
  uint8_t len = 0;
  if (frame_.ReadParam8(out, nina::kMacLength, &len) != Result::kOk ||
      len != nina::kMacLength) {
    frame_.Abort();
    return false;
  }
  return frame_.FinishReply() == Result::kOk;
}

// ---------------------------------------------------------------------------
// Station
// ---------------------------------------------------------------------------

bool NinaWifi::SetNetwork(const char* ssid, uint8_t ssid_len,
                          uint32_t timeout_ms) {
  if (frame_.BeginCommand(nina::Opcode::kSetNet, 1, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  frame_.PutParam8(ssid, ssid_len);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return false;
  }
  return ReadStatusReply(nina::Opcode::kSetNet, timeout_ms);
}

bool NinaWifi::SetPassphrase(const char* ssid, uint8_t ssid_len,
                             const char* passphrase, uint8_t passphrase_len,
                             uint32_t timeout_ms) {
  if (frame_.BeginCommand(nina::Opcode::kSetPassphrase, 2, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  frame_.PutParam8(ssid, ssid_len);
  frame_.PutParam8(passphrase, passphrase_len);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return false;
  }
  return ReadStatusReply(nina::Opcode::kSetPassphrase, timeout_ms);
}

bool NinaWifi::SetKey(const char* ssid, uint8_t ssid_len, uint8_t key_index,
                      const void* key, uint8_t key_len, uint32_t timeout_ms) {
  if (frame_.BeginCommand(nina::Opcode::kSetKey, 3, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  frame_.PutParam8(ssid, ssid_len);
  frame_.PutParamU8(key_index);
  frame_.PutParam8(key, key_len);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return false;
  }
  return ReadStatusReply(nina::Opcode::kSetKey, timeout_ms);
}

bool NinaWifi::Connect(const char* ssid, const char* passphrase,
                       uint32_t timeout_ms) {
  if (ssid == nullptr || passphrase == nullptr) {
    return false;
  }
  const size_t ssid_len = strlen(ssid);
  const size_t pass_len = strlen(passphrase);
  if (ssid_len == 0 || ssid_len > nina::kSsidMaxLength ||
      pass_len > nina::kPassphraseMaxLength) {
    return false;
  }

  // For WPA2 the module joins from kSetPassphrase alone: the command stores
  // the SSID/passphrase pair and starts the association. Sending kSetNet as
  // well would first kick off an open-network join, which fails (the module
  // reports 201, WIFI_REASON_NO_AP_FOUND) before the key is known. This
  // mirrors WiFiNINA, which calls only wifiSetPassphrase().
  //
  // The module answers the command only after its blocking join/DHCP step, so
  // the frame timeout must cover the whole connect budget, not a fixed short
  // value.
  if (!SetPassphrase(ssid, static_cast<uint8_t>(ssid_len), passphrase,
                     static_cast<uint8_t>(pass_len), timeout_ms)) {
    return false;
  }
  return WaitForConnect(timeout_ms);
}

bool NinaWifi::ConnectOpen(const char* ssid, uint32_t timeout_ms) {
  if (ssid == nullptr) {
    return false;
  }
  const size_t ssid_len = strlen(ssid);
  if (ssid_len == 0 || ssid_len > nina::kSsidMaxLength) {
    return false;
  }
  // kSetNet starts an open-network join and answers only after it finishes.
  if (!SetNetwork(ssid, static_cast<uint8_t>(ssid_len), timeout_ms)) {
    return false;
  }
  return WaitForConnect(timeout_ms);
}

bool NinaWifi::ConnectWep(const char* ssid, const char* key, uint8_t key_index,
                          uint32_t timeout_ms) {
  if (ssid == nullptr || key == nullptr) {
    return false;
  }
  const size_t ssid_len = strlen(ssid);
  const size_t key_len = strlen(key);
  if (ssid_len == 0 || ssid_len > nina::kSsidMaxLength ||
      key_len > nina::kWepKeyMaxLength) {
    return false;
  }
  // As with WPA2, kSetKey alone starts the association; kSetNet must not be
  // sent first or the module attempts an open-network join. The module replies
  // only after the blocking join, so use the full connect budget.
  if (!SetKey(ssid, static_cast<uint8_t>(ssid_len), key_index, key,
              static_cast<uint8_t>(key_len), timeout_ms)) {
    return false;
  }
  return WaitForConnect(timeout_ms);
}

bool NinaWifi::WaitForConnect(uint32_t timeout_ms) {
  const uint32_t start = link_.Millis();
  for (;;) {
    const uint8_t value = ConnectionStatus(2000);
    if (value == kConnected) {
      return true;
    }
    // Only terminal failures are reported by the module. kIdle, "no SSID",
    // "scan done" and "disconnected" are transient states during an attempt.
    if (value == 0xFF || value == kConnectFailed || value == kConnectionLost) {
      return false;
    }
    if (static_cast<uint32_t>(link_.Millis() - start) >= timeout_ms) {
      return false;
    }
    link_.DelayMs(kPollIntervalMs);
  }
}

bool NinaWifi::Disconnect(uint32_t timeout_ms) {
  if (!StartDummyCommand(nina::Opcode::kDisconnect, timeout_ms)) {
    return false;
  }
  return ReadStatusReply(nina::Opcode::kDisconnect, timeout_ms);
}

// ---------------------------------------------------------------------------
// Network configuration
// ---------------------------------------------------------------------------

bool NinaWifi::SetStaticIp(const Ipv4& ip, const Ipv4& gateway,
                           const Ipv4& subnet, uint32_t timeout_ms) {
  if (frame_.BeginCommand(nina::Opcode::kSetIpConfig, 4, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  frame_.PutParamU8(3);  // valid: IP + gateway + subnet
  PutIpv4Param(ip);
  PutIpv4Param(gateway);
  PutIpv4Param(subnet);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return false;
  }
  return ReadStatusReply(nina::Opcode::kSetIpConfig, timeout_ms);
}

bool NinaWifi::SetDns(const Ipv4& dns1, const Ipv4& dns2, uint32_t timeout_ms) {
  if (frame_.BeginCommand(nina::Opcode::kSetDnsConfig, 3, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  frame_.PutParamU8(dns2.IsZero() ? 1 : 2);
  PutIpv4Param(dns1);
  PutIpv4Param(dns2);
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return false;
  }
  return ReadStatusReply(nina::Opcode::kSetDnsConfig, timeout_ms);
}

bool NinaWifi::SetHostname(const char* hostname, uint32_t timeout_ms) {
  if (hostname == nullptr) {
    return false;
  }
  const size_t len = strlen(hostname);
  if (len == 0 || len > 255) {
    return false;
  }
  if (frame_.BeginCommand(nina::Opcode::kSetHostname, 1, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  frame_.PutParam8(hostname, static_cast<uint8_t>(len));
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return false;
  }
  return ReadStatusReply(nina::Opcode::kSetHostname, timeout_ms);
}

bool NinaWifi::NetworkInfo(NetworkAddresses& out, uint32_t timeout_ms) {
  if (!StartDummyCommand(nina::Opcode::kGetIpAddr, timeout_ms)) {
    return false;
  }
  if (frame_.BeginReply(nina::Opcode::kGetIpAddr, timeout_ms) != Result::kOk) {
    return false;
  }

  uint8_t buffer[3][4] = {{0}};
  uint8_t len = 0;
  bool ok = true;
  for (uint8_t i = 0; i < 3 && ok; ++i) {
    ok = frame_.ReadParam8(buffer[i], sizeof(buffer[i]), &len) == Result::kOk &&
         len == 4;
  }
  if (!ok) {
    frame_.Abort();
    return false;
  }
  if (frame_.FinishReply() != Result::kOk) {
    return false;
  }

  out.ip = Ipv4(buffer[0][0], buffer[0][1], buffer[0][2], buffer[0][3]);
  out.mask = Ipv4(buffer[1][0], buffer[1][1], buffer[1][2], buffer[1][3]);
  out.gateway = Ipv4(buffer[2][0], buffer[2][1], buffer[2][2], buffer[2][3]);
  return true;
}

bool NinaWifi::DnsServers(DnsAddresses& out, uint32_t timeout_ms) {
  if (!StartDummyCommand(nina::Opcode::kGetDnsConfig, timeout_ms)) {
    return false;
  }
  if (frame_.BeginReply(nina::Opcode::kGetDnsConfig, timeout_ms) !=
      Result::kOk) {
    return false;
  }

  uint8_t buffer[2][4] = {{0}};
  uint8_t len = 0;
  bool ok = true;
  for (uint8_t i = 0; i < 2 && ok; ++i) {
    ok = frame_.ReadParam8(buffer[i], sizeof(buffer[i]), &len) == Result::kOk &&
         len == 4;
  }
  if (!ok) {
    frame_.Abort();
    return false;
  }
  if (frame_.FinishReply() != Result::kOk) {
    return false;
  }

  out.primary = Ipv4(buffer[0][0], buffer[0][1], buffer[0][2], buffer[0][3]);
  out.secondary = Ipv4(buffer[1][0], buffer[1][1], buffer[1][2], buffer[1][3]);
  return true;
}

// ---------------------------------------------------------------------------
// Link information
// ---------------------------------------------------------------------------

uint8_t NinaWifi::ConnectionStatus(uint32_t timeout_ms) {
  uint8_t value = nina::kFail;
  if (!ReadSingleByte(nina::Opcode::kGetConnStatus, value, timeout_ms)) {
    return 0xFF;
  }
  return value;
}

uint8_t NinaWifi::ReasonCode(uint32_t timeout_ms) {
  uint8_t value = 0;
  if (!ReadSingleByte(nina::Opcode::kGetReasonCode, value, timeout_ms)) {
    return 0xFF;
  }
  return value;
}

bool NinaWifi::CurrentSsid(char* out, uint8_t cap, uint32_t timeout_ms) {
  if (out == nullptr || cap == 0) {
    return false;
  }
  out[0] = '\0';
  if (!StartDummyCommand(nina::Opcode::kGetCurrSsid, timeout_ms)) {
    return false;
  }
  if (frame_.BeginReply(nina::Opcode::kGetCurrSsid, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  uint8_t len = 0;
  if (frame_.ReadParam8(out, static_cast<uint8_t>(cap - 1), &len) !=
      Result::kOk) {
    return false;
  }
  out[len] = '\0';
  return frame_.FinishReply() == Result::kOk;
}

bool NinaWifi::CurrentBssid(uint8_t out[nina::kMacLength],
                            uint32_t timeout_ms) {
  if (!StartDummyCommand(nina::Opcode::kGetCurrBssid, timeout_ms)) {
    return false;
  }
  if (frame_.BeginReply(nina::Opcode::kGetCurrBssid, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  uint8_t len = 0;
  if (frame_.ReadParam8(out, nina::kMacLength, &len) != Result::kOk ||
      len != nina::kMacLength) {
    frame_.Abort();
    return false;
  }
  return frame_.FinishReply() == Result::kOk;
}

int32_t NinaWifi::CurrentRssi(uint32_t timeout_ms) {
  int32_t value = 0;
  if (!ReadInt32(nina::Opcode::kGetCurrRssi, value, timeout_ms)) {
    return 0;
  }
  return value;
}

uint8_t NinaWifi::CurrentEncryption(uint32_t timeout_ms) {
  uint8_t value = 0;
  if (!ReadSingleByte(nina::Opcode::kGetCurrEnct, value, timeout_ms)) {
    return 0xFF;
  }
  return value;
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

bool NinaWifi::StartScan(uint32_t timeout_ms) {
  if (frame_.RunCommand(nina::Opcode::kStartScanNetworks, 0, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  if (frame_.BeginReply(nina::Opcode::kStartScanNetworks, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  uint8_t status = nina::kFail;
  uint8_t len = 0;
  if (frame_.ReadParam8(&status, 1, &len) != Result::kOk) {
    frame_.Abort();
    return false;
  }
  if (frame_.FinishReply() != Result::kOk) {
    return false;
  }
  return status != nina::kFail;
}

int NinaWifi::Scan(ScanEntry* out, int max_entries, uint32_t timeout_ms) {
  if (out == nullptr || max_entries <= 0) {
    return 0;
  }
  if (!StartScan(timeout_ms)) {
    return -1;
  }

  if (frame_.RunCommand(nina::Opcode::kScanNetworks, 0, timeout_ms) !=
      Result::kOk) {
    return -1;
  }
  if (frame_.BeginReply(nina::Opcode::kScanNetworks, timeout_ms) !=
      Result::kOk) {
    return -1;
  }

  const uint8_t total = frame_.ReplyParamCount();
  int count = 0;
  for (uint8_t i = 0; i < total; ++i) {
    char ssid[nina::kSsidMaxLength + 1] = {0};
    uint8_t len = 0;
    if (frame_.ReadParam8(ssid, nina::kSsidMaxLength, &len) != Result::kOk) {
      return -1;
    }
    ssid[len] = '\0';
    if (count < max_entries && count < nina::kNetworksListMax) {
      ScanEntry& entry = out[count];
      memset(&entry, 0, sizeof(entry));
      memcpy(entry.ssid, ssid, static_cast<size_t>(len) + 1);
      ++count;
    }
  }
  if (frame_.FinishReply() != Result::kOk) {
    return -1;
  }

  for (int i = 0; i < count; ++i) {
    QueryScanEntry(static_cast<uint8_t>(i), out[i], timeout_ms);
  }
  return count;
}

bool NinaWifi::QueryScanEntry(uint8_t index, ScanEntry& entry,
                              uint32_t timeout_ms) {
  uint8_t buffer[6] = {0};
  uint8_t len = 0;

  if (ReadIndexed(frame_, nina::Opcode::kGetIdxRssi, index, buffer, 4, len,
                  timeout_ms) &&
      len == 4) {
    entry.rssi = DecodeInt32Le(buffer);
  }

  if (ReadIndexed(frame_, nina::Opcode::kGetIdxEnct, index, buffer, 1, len,
                  timeout_ms) &&
      len == 1) {
    entry.encryption = buffer[0];
  }

  if (ReadIndexed(frame_, nina::Opcode::kGetIdxBssid, index, buffer, 6, len,
                  timeout_ms) &&
      len == 6) {
    memcpy(entry.bssid, buffer, 6);
  }

  if (ReadIndexed(frame_, nina::Opcode::kGetIdxChannel, index, buffer, 1, len,
                  timeout_ms) &&
      len == 1) {
    entry.channel = buffer[0];
  }

  return true;
}

// ---------------------------------------------------------------------------
// DNS
// ---------------------------------------------------------------------------

bool NinaWifi::Resolve(const char* hostname, Ipv4& out, uint32_t timeout_ms) {
  if (hostname == nullptr) {
    return false;
  }
  const size_t len = strlen(hostname);
  if (len == 0 || len > 255) {
    return false;
  }

  if (frame_.BeginCommand(nina::Opcode::kReqHostByName, 1, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  frame_.PutParam8(hostname, static_cast<uint8_t>(len));
  if (frame_.FinishCommand(timeout_ms) != Result::kOk) {
    return false;
  }
  if (!ReadStatusReply(nina::Opcode::kReqHostByName, timeout_ms)) {
    return false;
  }

  if (frame_.RunCommand(nina::Opcode::kGetHostByName, 0, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  if (frame_.BeginReply(nina::Opcode::kGetHostByName, timeout_ms) !=
      Result::kOk) {
    return false;
  }
  uint8_t buffer[4] = {0};
  uint8_t got_len = 0;
  if (frame_.ReadParam8(buffer, sizeof(buffer), &got_len) != Result::kOk ||
      got_len != 4) {
    frame_.Abort();
    return false;
  }
  if (frame_.FinishReply() != Result::kOk) {
    return false;
  }

  out = Ipv4(buffer[0], buffer[1], buffer[2], buffer[3]);
  return true;
}

}  // namespace dash
