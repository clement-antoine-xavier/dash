// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#ifndef DASHNET_DASH_NINA_WIFI_H_
#define DASHNET_DASH_NINA_WIFI_H_

#include <stdint.h>

#include "dash/nina_commands.h"
#include "dash/nina_frame.h"
#include "dash/nina_link.h"
#include "dash/types.h"

namespace dash {

// Station control and DNS built on top of the frame layer.
class NinaWifi {
 public:
  explicit NinaWifi(NinaLink& link) : link_(link), frame_(link) {}

  // Resets the module and verifies that it answers the protocol.
  bool Begin(uint32_t ready_timeout_ms = 5000);

  // ---- identification -----------------------------------------------------
  const char* FirmwareVersion() const { return fw_; }
  uint32_t FirmwareVersionU32(uint32_t timeout_ms = 2000);
  bool Mac(uint8_t out[nina::kMacLength], uint32_t timeout_ms = 2000);

  // ---- station ------------------------------------------------------------
  bool SetNetwork(const char* ssid, uint8_t ssid_len,
                  uint32_t timeout_ms = 2000);
  bool SetPassphrase(const char* ssid, uint8_t ssid_len, const char* passphrase,
                     uint8_t passphrase_len, uint32_t timeout_ms = 2000);
  bool SetKey(const char* ssid, uint8_t ssid_len, uint8_t key_index,
              const void* key, uint8_t key_len, uint32_t timeout_ms = 2000);

  bool Connect(const char* ssid, const char* passphrase,
               uint32_t timeout_ms = 30000);
  bool ConnectOpen(const char* ssid, uint32_t timeout_ms = 30000);
  bool ConnectWep(const char* ssid, const char* key, uint8_t key_index = 0,
                  uint32_t timeout_ms = 30000);

  bool Disconnect(uint32_t timeout_ms = 2000);
  bool WaitForConnect(uint32_t timeout_ms);

  // ---- network configuration ---------------------------------------------
  // Addresses are sent to the module in its native (little endian uint32)
  // wire format; this is handled internally.
  bool SetStaticIp(const Ipv4& ip, const Ipv4& gateway, const Ipv4& subnet,
                   uint32_t timeout_ms = 2000);
  bool SetDns(const Ipv4& dns1, const Ipv4& dns2 = Ipv4(),
              uint32_t timeout_ms = 2000);
  bool SetHostname(const char* hostname, uint32_t timeout_ms = 2000);

  bool NetworkInfo(NetworkAddresses& out, uint32_t timeout_ms = 2000);
  bool DnsServers(DnsAddresses& out, uint32_t timeout_ms = 2000);

  // ---- link information ---------------------------------------------------
  uint8_t ConnectionStatus(uint32_t timeout_ms = 2000);
  uint8_t ReasonCode(uint32_t timeout_ms = 2000);
  bool CurrentSsid(char* out, uint8_t cap, uint32_t timeout_ms = 2000);
  bool CurrentBssid(uint8_t out[nina::kMacLength], uint32_t timeout_ms = 2000);
  int32_t CurrentRssi(uint32_t timeout_ms = 2000);
  uint8_t CurrentEncryption(uint32_t timeout_ms = 2000);

  // ---- scanning -----------------------------------------------------------
  bool StartScan(uint32_t timeout_ms = 10000);
  // Returns the number of entries written, or -1 on a transport error.
  int Scan(ScanEntry* out, int max_entries, uint32_t timeout_ms = 10000);

  // ---- DNS ----------------------------------------------------------------
  bool Resolve(const char* hostname, Ipv4& out, uint32_t timeout_ms = 10000);

  NinaFrame& Frame() { return frame_; }
  NinaLink& Link() { return link_; }

 private:
  bool Identify(uint32_t timeout_ms);
  bool ReadSingleByte(nina::Opcode opcode, uint8_t& out, uint32_t timeout_ms);
  bool ReadString(nina::Opcode opcode, char* out, uint8_t cap, uint8_t& len,
                  uint32_t timeout_ms);
  bool ReadStatusReply(nina::Opcode opcode, uint32_t timeout_ms);
  bool StartDummyCommand(nina::Opcode opcode, uint32_t timeout_ms);
  bool ReadInt32(nina::Opcode opcode, int32_t& out, uint32_t timeout_ms);
  bool QueryScanEntry(uint8_t index, ScanEntry& entry, uint32_t timeout_ms);
  void PutIpv4Param(const Ipv4& address);

  NinaLink& link_;
  NinaFrame frame_;
  char fw_[16] = {0};
};

}  // namespace dash

#endif  // DASHNET_DASH_NINA_WIFI_H_
