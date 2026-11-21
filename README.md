# DashNet

A from-scratch WiFi driver for the **Arduino MKR WiFi 1010**, written without
`WiFiNINA` or `Arduino_SpiNINA`.

The board does not have a SPI WiFi radio attached to the SAMD21. It carries a
**NINA-W102** module: an ESP32 running Arduino's NINA firmware, which owns the
802.11 stack, WPA2, DHCP, DNS, TCP/IP and TLS. The SAMD21 talks to it over
**SERCOM4 / SPI1** using a byte-oriented command protocol. This library
implements that transport and protocol and exposes a small C++ API.

```
┌─────────────────┐   SPI1 + ACK/RESET   ┌──────────────────────────┐
│ SAMD21 (host)   │ ───────────────────> │ NINA-W102 (ESP32)        │
│ DashNet         │                      │ NINA firmware: 802.11,   │
│ - SPI transport │                      │ WPA2, DHCP, DNS, TCP/IP, │
│ - command codec │                      │ TLS                      │
└─────────────────┘                      └──────────────────────────┘
```

## Features

- Reset/bring-up and firmware/MAC identification
- Station mode: WPA2, WEP, open networks, disconnect, reason codes
- Network scan (SSID, BSSID, RSSI, channel, encryption)
- DHCP and static IPv4, DNS servers, hostname
- DNS resolution with a local IPv4 literal fast path
- TCP client with chunked reads and writes
- TLS client (`TLS_MODE`): SNI and certificate validation inside the module
- UDP sockets and SNTP
- TCP server with explicit client socket ownership
- No dynamic allocation, no global singletons, no Arduino `Client`/`Server`
  inheritance

## Status

| Phase | Scope | State |
|---|---|---|
| P0 | Scaffold, SPI frame codec, simulator, native tests | done |
| P1 | `NinaSpiLink` hardware bring-up | done (firmware 1.4.8 on hardware) |
| P2 | Station mode: connect, scan, IP config | done |
| P3 | DNS resolution | done |
| P4 | TCP client | done |
| P5 | TLS (`TLS_MODE`) | done |
| P6 | UDP + SNTP, TCP server | done |
| P7 | Hardening, Google style, docs, CI | done |
| P8 | Optional: `TLS_BEARSSL_MODE` with ATECC608A | not implemented |

Native tests and host-side protocol checks pass. On-board verification on a MKR
WiFi 1010 running NINA firmware 1.4.8 covered reset/bring-up (firmware and MAC
read), network scan, WPA2 join with DHCP, DNS resolution and a TCP/HTTP GET.
TLS, UDP/SNTP and the TCP server have not yet been exercised on hardware.

## Layout

```
dash/
├── dash.ino                     # bring-up sketch (firmware + MAC + status)
├── library.properties
├── .clang-format                # Google style
├── include/
│   └── dash/
│       ├── dash_net.h           # public API
│       ├── types.h              # Status, Ipv4, ScanEntry, NetworkAddresses
│       ├── nina_commands.h      # opcodes, framing constants, limits
│       ├── nina_link.h          # transport interface
│       ├── nina_frame.h         # frame codec (host-testable)
│       ├── nina_spi_link.h      # SPI1 + CS/ACK/RESET/GPIO0
│       ├── nina_wifi.h          # station, scan, DNS, IP configuration
│       ├── nina_socket.h        # TCP/UDP sockets, server, TLS
│       ├── tcp_socket.h
│       ├── tcp_server.h
│       └── udp_socket.h
├── src/
│   ├── dash_net.h               # Arduino library forwarder
│   ├── dash_net.cc
│   ├── tcp_socket.cc
│   ├── tcp_server.cc
│   ├── udp_socket.cc
│   └── dash/
│       ├── nina_frame.cc
│       ├── nina_socket.cc
│       ├── nina_spi_link.cc
│       └── nina_wifi.cc
├── examples/
│   ├── Scan/                    # list visible networks
│   ├── Connect/                 # join a network, print IP/DNS/RSSI
│   ├── HttpGet/                 # plain HTTP GET
│   ├── TlsGet/                  # HTTPS GET with module-side TLS
│   ├── TcpServer/               # TCP echo server
│   └── UdpNtp/                  # SNTP over UDP
├── test/
│   ├── frame_codec_test.cc      # native frame tests
│   ├── station_test.cc          # native station/scan/DNS tests
│   ├── socket_test.cc           # native TCP/UDP/server tests
│   ├── sim/fake_nina.{h,cc}     # NINA module simulator
│   └── Makefile
├── Makefile                     # tests, format check, board builds
└── .gitignore
```

## Using it

The repository doubles as an Arduino sketch and as an Arduino library.

Builds that add `-Iinclude` (the bundled `Makefile` and
`build.sh --include-dir include` do this) include the public header directly:

```cpp
#include <dash/dash_net.h>
```

Arduino-style library builds and the bundled examples go through the
forwarder at the root of `src/`:

```cpp
#include <dash_net.h>
```

`dash.ino` includes the forwarder relative to the sketch root:

```cpp
#include "src/dash_net.h"
```

```cpp
#include <dash/dash_net.h>

dash::Net net;

void setup() {
  Serial.begin(115200);
  if (!net.Begin()) {
    Serial.println("NINA bring-up failed");
    return;
  }
  if (!net.Connect("ssid", "passphrase", 30000)) {
    Serial.println(net.ReasonCode());
    return;
  }

  dash::TcpSocket client(net);
  if (client.Connect("example.com", 80)) {
    const char request[] = "GET / HTTP/1.1\r\nHost: example.com\r\n"
                           "Connection: close\r\n\r\n";
    client.Write(request, sizeof(request) - 1);
    client.Flush();

    char buffer[128];
    int n;
    while ((n = client.Read(buffer, sizeof(buffer))) > 0) {
      Serial.write(buffer, n);
    }
    client.Close();
  }
}
```

## API overview

```cpp
const char* Version();
const char* ReasonName(uint8_t reason);  // ESP32/NINA disconnect reason as text

class Net {
  bool Begin(uint32_t timeout_ms = 5000);

  bool Connect(const char* ssid, const char* pass, uint32_t timeout_ms = 30000);
  bool ConnectOpen(const char* ssid, uint32_t timeout_ms = 30000);
  bool ConnectWep(const char* ssid, const char* key, uint8_t key_index = 0,
                  uint32_t timeout_ms = 30000);
  bool Disconnect();
  Status ConnectionStatus();
  uint8_t ReasonCode();

  bool SetStaticIp(const Ipv4& ip, const Ipv4& gateway, const Ipv4& subnet);
  bool SetDns(const Ipv4& dns1, const Ipv4& dns2 = Ipv4());
  bool SetHostname(const char* hostname);
  bool NetworkInfo(NetworkAddresses& out);
  bool DnsServers(DnsAddresses& out);

  bool CurrentSsid(char* out, size_t cap);
  bool CurrentBssid(uint8_t out[6]);
  int32_t Rssi();
  uint8_t Encryption();

  int Scan(ScanEntry* out, int max_entries);
  bool Resolve(const char* hostname, Ipv4& out);
  const char* FirmwareVersion();
  uint32_t FirmwareVersionU32();
  bool Mac(uint8_t out[6]);
};

class TcpSocket {
  TcpSocket(Net& net);
  bool Connect(const Ipv4& ip, uint16_t port, uint32_t timeout_ms = 10000);
  bool Connect(const char* host, uint16_t port, uint32_t timeout_ms = 10000);
  bool ConnectTls(const char* host, uint16_t port, uint32_t timeout_ms = 20000);
  int Write(const void* data, size_t len);
  int Read(void* buffer, size_t len);
  bool Flush(uint32_t timeout_ms = 2500);
  int Available();
  int Peek();
  bool Connected();
  nina::SocketState State();
  bool Remote(Ipv4& ip, uint16_t& port);
  void Close();
};

class TcpServer {
  TcpServer(Net& net);
  bool Begin(uint16_t port);
  bool Accept(TcpSocket& client);
  bool End();
  nina::SocketState State();
};

class UdpSocket {
  UdpSocket(Net& net);
  bool Begin(uint16_t port);
  bool BeginPacket(const Ipv4& ip, uint16_t port);
  bool BeginPacket(const char* host, uint16_t port);
  int Write(const void* data, size_t len);
  bool EndPacket();
  int ParsePacket();
  int Read(void* buffer, size_t len);
  int Available();
};
```

Notes:

- `Read()` returns the number of bytes copied, `0` when nothing is buffered,
  and a negative value on a socket error.
- `Available()` is zero until the module's data-ready line goes high, then
  reports the number of bytes the module holds.
- `Write()` hands bytes to the module in chunks of at most 1 KiB.
  `Flush()` waits for the module to confirm that the peer acknowledged them.
- Sockets are explicit objects. The module provides five socket slots; the
  driver asks the module for a free one on every connect/listen, so multiple
  sockets can coexist.

## Building and testing

Native tests (no board, no Arduino toolchain):

```sh
make test
```

Compile the sketch and every example for the board through `build.sh`:

```sh
make compile            # just the sketch
make compile-examples   # all examples
make                    # tests + sketch + all examples
make strict             # same, but --clean (cold cache) for every build
```

`build.sh` compiles with `-Wall -Wextra -Wpedantic -Wshadow -Wcast-align
-Wconversion -Wsign-conversion -Wdouble-promotion -Wundef -Wformat=2
-Wformat-security` and `--werror`. A few diagnostics are produced by the
Arduino SAMD core, CMSIS and the bundled SPI library; those specific warning
classes stay warnings (they are listed in `build.sh`) so that a clean
`--werror` build fails only on project code.

Direct use:

```sh
./build.sh --werror --clean --include-dir include \
  -b arduino:samd:mkrwifi1010 .
./build.sh --werror --clean --include-dir include \
  -b arduino:samd:mkrwifi1010 --library . examples/TlsGet
```

`build.sh --help` lists all options (`-b`, `-o`, `-w`, `--library`,
`--include-dir`, `--clean`, `--werror`).

## Code style

The project follows the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
and is formatted with `clang-format` using `BasedOnStyle: Google`
(see `.clang-format`). In practice this means:

- snake_case file names and `.cc` sources
- `PascalCase` functions and `kConstantName` enumerators
- `snake_case` variables and parameters, trailing underscore for class members
- `enum class` for every enumeration
- `#ifndef DASHNET_<PATH>_<FILE>_H_` header guards
- includes grouped and sorted (related header, C, C++, libraries, project)

Useful targets:

```sh
make format         # apply clang-format
make format-check   # fail if anything is not formatted
make headers-check  # compile every header on its own
make check          # format-check + cold strict build + tests
```

Arduino-mandated exceptions, documented here because they differ from the
guide:

- The sketch is `dash.ino` (Arduino requires a directory-matching `.ino`).
- The language level is C++11 (`gnu++11`), because that is what the Arduino
  SAMD core provides; the guide targets C++20.
- Public headers live in `include/dash/` and are included as `"dash/..."`;
  builds must add `-Iinclude` (`make` and `build.sh --include-dir include`
  do this).
- `src/dash_net.h` is a one-line forwarder to `include/dash/dash_net.h`.
  The Arduino library resolver only offers headers at the root of `src/` as
  candidates, so this shim is required for `--library` builds. The stock
  Arduino IDE additionally needs
  `compiler.cpp.extra_flags=-I<repo>/include`; the supported build entry
  points are `make` and `build.sh`.

## Protocol notes

Frames use `0xE0` as start marker, `0xEE` as end marker and `0xEF` as error
marker. A reply sets bit 7 of the opcode.

```
host -> module:  0xE0 | cmd      | nParam | [len + data]*        | 0xEE
module -> host:  0xE0 | cmd|0x80 | nParam | [len8/len16 + data]* | 0xEE
```

Non-obvious details that the driver reproduces:

- Command frames are padded with `0xFF` bytes to a multiple of four.
- Commands with the `0x40` flag set (`kSendDataTcp`, `kGetDatabufTcp`,
  `kInsertDatabuf`, ...) use 16-bit parameter lengths.
- The `ACK` line is low when the module is ready and high while a transaction
  is selected. The reply is read in a second chip-select window after the
  module lowers `ACK` again.
- Reset sequence: `GPIO0` high, `RESETN` low for 10 ms, release, wait 750 ms,
  then release `GPIO0` and switch it to input.
- IPv4 addresses travel as a native-endian `uint32_t` when written to the
  module, which on the little-endian SAMD21 means the address bytes are
  reversed on the wire. Addresses read back (IP configuration, DNS) are in
  natural order.
- `kAvailDataTcp` and `kSendDataTcp` replies carry 16-bit little-endian
  values; the frame layer reads them as short parameters.
- A join is started by the configuration command itself: `kSetPassphrase` for
  WPA2, `kSetKey` for WEP and `kSetNet` for open networks. The module replies
  only after that blocking join (and DHCP), so the frame timeout must cover the
  whole connect budget.
- NINA firmware 1.4.x ignores the `startClientTcp` timeout parameter and can
  block a connect for about 20 s. The driver waits for and drains that reply
  before reusing the socket pool, so a failed connect cannot desynchronise the
  next transaction.

## Limitations

- IPv4 only (the module protocol has no IPv6).
- Module-side TLS validates against the firmware's built-in CA bundle; custom
  CA injection is not implemented. `TLS_BEARSSL_MODE` (SAMD21-side TLS with
  the ATECC608A trust anchor) is a separate optional phase.
- A scan returns at most 10 networks, the limit of the module's reply.
- The driver requires NINA firmware 1.4.0 or newer for the extended
  `kStartClientTcp` command used by the TLS and hostname paths.
- `DnsServers()` requires NINA firmware 3.0 or newer. Firmware 1.4.x has no
  handler for `kGetDnsConfig` and answers with the error marker; `DnsServers()`
  returns false there. Name resolution still works because it runs inside the
  module.

## Safety

The library never writes the module firmware. The worst case for a bug is a
module reset, not a bricked board.

## License

MIT. The NINA protocol opcodes and framing rules are interoperability facts.
No code from `WiFiNINA` (LGPL-2.1) or `Arduino_SpiNINA` (MPL-2.0) is linked
or copied.
