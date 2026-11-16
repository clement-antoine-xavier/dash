// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#include "dash/dash_net.h"

namespace dash {
namespace {

constexpr char kVersion[] = "0.3.0";

}  // namespace

const char* Version() { return kVersion; }

Net::Net() : link_(), wifi_(link_), begun_(false) {}

bool Net::Begin(uint32_t timeout_ms) {
  begun_ = wifi_.Begin(timeout_ms);
  return begun_;
}

uint32_t Net::FirmwareVersionU32() { return wifi_.FirmwareVersionU32(); }

bool Net::Mac(uint8_t out[6]) { return wifi_.Mac(out); }

bool Net::Connect(const char* ssid, const char* passphrase,
                  uint32_t timeout_ms) {
  return wifi_.Connect(ssid, passphrase, timeout_ms);
}

bool Net::ConnectOpen(const char* ssid, uint32_t timeout_ms) {
  return wifi_.ConnectOpen(ssid, timeout_ms);
}

bool Net::ConnectWep(const char* ssid, const char* key, uint8_t key_index,
                     uint32_t timeout_ms) {
  return wifi_.ConnectWep(ssid, key, key_index, timeout_ms);
}

bool Net::Disconnect() { return wifi_.Disconnect(); }

Status Net::ConnectionStatus() { return ToStatus(wifi_.ConnectionStatus()); }

uint8_t Net::ReasonCode() { return wifi_.ReasonCode(); }

bool Net::SetStaticIp(const Ipv4& ip, const Ipv4& gateway, const Ipv4& subnet) {
  return wifi_.SetStaticIp(ip, gateway, subnet);
}

bool Net::SetDns(const Ipv4& dns1, const Ipv4& dns2) {
  return wifi_.SetDns(dns1, dns2);
}

bool Net::SetHostname(const char* hostname) {
  return wifi_.SetHostname(hostname);
}

bool Net::NetworkInfo(NetworkAddresses& out) { return wifi_.NetworkInfo(out); }

bool Net::DnsServers(DnsAddresses& out) { return wifi_.DnsServers(out); }

bool Net::CurrentSsid(char* out, size_t cap) {
  if (cap == 0 || cap > 255) {
    return false;
  }
  return wifi_.CurrentSsid(out, static_cast<uint8_t>(cap));
}

bool Net::CurrentBssid(uint8_t out[6]) { return wifi_.CurrentBssid(out); }

int32_t Net::Rssi() { return wifi_.CurrentRssi(); }

uint8_t Net::Encryption() { return wifi_.CurrentEncryption(); }

int Net::Scan(ScanEntry* out, int max_entries) {
  return wifi_.Scan(out, max_entries);
}

bool Net::Resolve(const char* hostname, Ipv4& out) {
  if (ParseIpv4(hostname, out)) {
    return true;
  }
  return wifi_.Resolve(hostname, out);
}

}  // namespace dash
