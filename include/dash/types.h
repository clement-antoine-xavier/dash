// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT
//
// Small value types shared by the public API and the protocol layer.

#ifndef DASHNET_DASH_TYPES_H_
#define DASHNET_DASH_TYPES_H_

#include <stddef.h>
#include <stdint.h>

namespace dash {

// Status values reported by the NINA module (WL_* in the Arduino world).
enum class Status : uint8_t {
  kNoModule = 255,
  kIdle = 0,
  kNoSsid = 1,
  kScanDone = 2,
  kConnected = 3,
  kConnectFailed = 4,
  kConnectionLost = 5,
  kDisconnected = 6,
  kApListening = 7,
  kApConnected = 8,
  kApFailed = 9,
};

inline Status ToStatus(uint8_t value) {
  switch (value) {
    case 0:
      return Status::kIdle;
    case 1:
      return Status::kNoSsid;
    case 2:
      return Status::kScanDone;
    case 3:
      return Status::kConnected;
    case 4:
      return Status::kConnectFailed;
    case 5:
      return Status::kConnectionLost;
    case 6:
      return Status::kDisconnected;
    case 7:
      return Status::kApListening;
    case 8:
      return Status::kApConnected;
    case 9:
      return Status::kApFailed;
    default:
      return Status::kNoModule;
  }
}

// One entry of a WiFi scan.
struct ScanEntry {
  char ssid[33];
  uint8_t bssid[6];
  uint8_t channel;
  uint8_t encryption;  // 802.11 auth/enc type reported by the module
  int32_t rssi;        // dBm
};

// Minimal Ipv4 address. Deliberately independent from the Arduino IPAddress
// class so the protocol layer can be compiled and unit-tested on a host.
struct Ipv4 {
  uint8_t bytes[4];

  Ipv4() : bytes{0, 0, 0, 0} {}
  Ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes{a, b, c, d} {}

  bool IsZero() const {
    return bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0;
  }

  // Value in host order (a << 24 | b << 16 | c << 8 | d).
  uint32_t ToU32() const {
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) |
           static_cast<uint32_t>(bytes[3]);
  }

  // The NINA wire format transports the address as a native-endian uint32_t.
  // On the little-endian SAMD21 that means the bytes go out in reverse order.
  void ToWire(uint8_t out[4]) const {
    out[0] = bytes[3];
    out[1] = bytes[2];
    out[2] = bytes[1];
    out[3] = bytes[0];
  }

  static Ipv4 FromWire(const uint8_t in[4]) {
    return Ipv4(in[3], in[2], in[1], in[0]);
  }

  // Writes "a.b.c.d\0" and returns the number of characters written
  // (excluding the terminator). Returns 0 if the buffer is too small.
  size_t ToString(char* out, size_t cap) const {
    if (out == nullptr || cap < 8) {
      if (out != nullptr && cap > 0) {
        out[0] = '\0';
      }
      return 0;
    }
    size_t pos = 0;
    for (int i = 0; i < 4; ++i) {
      if (i != 0) {
        out[pos++] = '.';
      }
      uint8_t value = bytes[i];
      char digits[3];
      uint8_t count = 0;
      if (value == 0) {
        digits[count++] = '0';
      } else {
        while (value != 0 && count < 3) {
          digits[count++] = static_cast<char>('0' + (value % 10));
          value /= 10;
        }
      }
      while (count > 0 && pos + 1 < cap) {
        out[pos++] = digits[--count];
      }
    }
    out[pos] = '\0';
    return pos;
  }

  bool operator==(const Ipv4& other) const {
    return bytes[0] == other.bytes[0] && bytes[1] == other.bytes[1] &&
           bytes[2] == other.bytes[2] && bytes[3] == other.bytes[3];
  }

  bool operator!=(const Ipv4& other) const { return !(*this == other); }
};

// Result of Net::NetworkInfo().
struct NetworkAddresses {
  Ipv4 ip;
  Ipv4 mask;
  Ipv4 gateway;
};

// Result of Net::DnsServers().
struct DnsAddresses {
  Ipv4 primary;
  Ipv4 secondary;
};

// Parses "a.b.c.d" (0-255 per octet). Returns false on any deviation.
inline bool ParseIpv4(const char* text, Ipv4& out) {
  if (text == nullptr) {
    return false;
  }
  uint16_t parts[4] = {0, 0, 0, 0};
  int part = 0;
  const char* p = text;

  while (*p != '\0') {
    if (part >= 4) {
      return false;
    }
    if (*p < '0' || *p > '9') {
      return false;
    }
    uint16_t value = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
      value = static_cast<uint16_t>(value * 10 + (*p - '0'));
      if (value > 255 || ++digits > 3) {
        return false;
      }
      ++p;
    }
    parts[part++] = value;
    if (*p == '.') {
      ++p;
      if (*p == '\0') {
        return false;
      }
    } else if (*p != '\0') {
      return false;
    }
  }

  if (part != 4) {
    return false;
  }
  out = Ipv4(static_cast<uint8_t>(parts[0]), static_cast<uint8_t>(parts[1]),
             static_cast<uint8_t>(parts[2]), static_cast<uint8_t>(parts[3]));
  return true;
}

}  // namespace dash

#endif  // DASHNET_DASH_TYPES_H_
