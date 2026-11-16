// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT
//
// Public entry point. Include this header from a sketch or application.

#ifndef DASHNET_DASH_DASH_NET_H_
#define DASHNET_DASH_DASH_NET_H_

#include <stddef.h>
#include <stdint.h>

#include "dash/nina_spi_link.h"
#include "dash/nina_wifi.h"
#include "dash/tcp_server.h"
#include "dash/tcp_socket.h"
#include "dash/types.h"
#include "dash/udp_socket.h"

namespace dash {

// Library version, e.g. "0.3.0".
const char* Version();

// One network interface bound to the onboard NINA-W102.
class Net {
 public:
  Net();
  Net(const Net&) = delete;
  Net& operator=(const Net&) = delete;

  // Resets the module, performs the SPI handshake and reads the firmware
  // version. Returns false if the module does not answer.
  bool Begin(uint32_t timeout_ms = 5000);

  // True after a successful Begin().
  bool IsBegun() const { return begun_; }

  // ---- identification -----------------------------------------------------
  const char* FirmwareVersion() const { return wifi_.FirmwareVersion(); }
  uint32_t FirmwareVersionU32();
  bool Mac(uint8_t out[6]);

  // ---- station ------------------------------------------------------------
  // Joins a WPA2 network, a WEP network or an open network and waits for the
  // DHCP lease. Returns false when the module reports a terminal failure or
  // the timeout expires.
  bool Connect(const char* ssid, const char* passphrase,
               uint32_t timeout_ms = 30000);
  bool ConnectOpen(const char* ssid, uint32_t timeout_ms = 30000);
  bool ConnectWep(const char* ssid, const char* key, uint8_t key_index = 0,
                  uint32_t timeout_ms = 30000);

  // Leaves the current network. The module keeps its configuration.
  bool Disconnect();

  // Current module status and the reason code of the last disconnect.
  Status ConnectionStatus();
  uint8_t ReasonCode();

  // ---- configuration ------------------------------------------------------
  // Static IPv4 configuration. Call before Connect() to skip DHCP.
  bool SetStaticIp(const Ipv4& ip, const Ipv4& gateway, const Ipv4& subnet);
  bool SetDns(const Ipv4& dns1, const Ipv4& dns2 = Ipv4());
  bool SetHostname(const char* hostname);

  // Reads the current address, mask and gateway from the module.
  bool NetworkInfo(NetworkAddresses& out);
  bool DnsServers(DnsAddresses& out);

  // ---- link information ---------------------------------------------------
  bool CurrentSsid(char* out, size_t cap);
  bool CurrentBssid(uint8_t out[6]);
  int32_t Rssi();
  uint8_t Encryption();

  // ---- scanning -----------------------------------------------------------
  // Scans for networks. Returns the number of entries written, or -1 on a
  // transport error.
  int Scan(ScanEntry* out, int max_entries);

  // ---- DNS ----------------------------------------------------------------
  // Resolves a hostname. Ipv4 literals are parsed locally without touching
  // the module.
  bool Resolve(const char* hostname, Ipv4& out);

  // Escape hatch for the socket layer and for diagnostics.
  NinaWifi& Driver() { return wifi_; }
  NinaFrame& Frame() { return wifi_.Frame(); }

 private:
  NinaSpiLink link_;
  NinaWifi wifi_;
  bool begun_;
};

}  // namespace dash

#endif  // DASHNET_DASH_DASH_NET_H_
