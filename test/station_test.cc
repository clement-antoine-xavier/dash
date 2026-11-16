// DashNet - native unit tests for station, scanning and DNS.
// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dash/nina_commands.h"
#include "dash/nina_wifi.h"
#include "dash/types.h"
#include "sim/fake_nina.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using dashtest::FakeNina;
using Params = std::vector<std::vector<uint8_t>>;
namespace nina = dash::nina;

void TestBeginReadsFirmware() {
  FakeNina sim;
  dash::NinaWifi wifi(sim);
  sim.SetReply(nina::Opcode::kGetFwVersion, {{'1', '.', '4', '.', '8'}});

  CHECK(wifi.Begin());
  CHECK(std::strcmp(wifi.FirmwareVersion(), "1.4.8") == 0);
  CHECK(sim.Frames().size() == 1);
  CHECK(sim.Frames()[0].cmd == nina::Opcode::kGetFwVersion);
  CHECK(sim.Frames()[0].params.empty());
}

void TestConnectWpa() {
  FakeNina sim;
  dash::NinaWifi wifi(sim);
  sim.SetReply(nina::Opcode::kGetFwVersion, {{'1', '.', '4', '.', '8'}});
  CHECK(wifi.Begin());

  sim.SetStatusReply(nina::Opcode::kSetNet, nina::kAck);
  sim.SetStatusReply(nina::Opcode::kSetPassphrase, nina::kAck);

  int polls = 0;
  sim.SetHandler(nina::Opcode::kGetConnStatus,
                 [&polls](const FakeNina::Frame&) -> Params {
                   ++polls;
                   const uint8_t status = (polls < 3) ? 0 : 3;
                   return Params{{status}};
                 });

  const size_t base = sim.Frames().size();
  CHECK(wifi.Connect("testnet", "password123"));

  CHECK(polls == 3);
  CHECK(sim.Frames().size() == base + 2 + static_cast<size_t>(polls));
  CHECK(sim.Frames()[base].cmd == nina::Opcode::kSetNet);
  CHECK(sim.Frames()[base].params.size() == 1);
  CHECK(std::string(sim.Frames()[base].params[0].begin(),
                    sim.Frames()[base].params[0].end()) == "testnet");
  CHECK(sim.Frames()[base + 1].cmd == nina::Opcode::kSetPassphrase);
  CHECK(sim.Frames()[base + 1].params.size() == 2);
  CHECK(std::string(sim.Frames()[base + 1].params[1].begin(),
                    sim.Frames()[base + 1].params[1].end()) == "password123");
}

void TestConnectFailure() {
  FakeNina sim;
  dash::NinaWifi wifi(sim);
  sim.SetReply(nina::Opcode::kGetFwVersion, {{'1', '.', '4', '.', '8'}});
  CHECK(wifi.Begin());

  sim.SetStatusReply(nina::Opcode::kSetNet, nina::kAck);
  sim.SetStatusReply(nina::Opcode::kSetPassphrase, nina::kAck);
  sim.SetStatusReply(nina::Opcode::kGetConnStatus, 4);  // connect failed

  CHECK(!wifi.Connect("testnet", "wrongpass"));
}

void TestConnectOpenAndWep() {
  FakeNina sim;
  dash::NinaWifi wifi(sim);
  sim.SetReply(nina::Opcode::kGetFwVersion, {{'1', '.', '4', '.', '8'}});
  CHECK(wifi.Begin());
  sim.SetStatusReply(nina::Opcode::kSetNet, nina::kAck);
  sim.SetStatusReply(nina::Opcode::kSetKey, nina::kAck);
  sim.SetStatusReply(nina::Opcode::kGetConnStatus, 3);

  CHECK(wifi.ConnectOpen("open-net"));
  CHECK(wifi.ConnectWep("wep-net", "abcde", 1));

  const auto& frames = sim.Frames();
  bool saw_key = false;
  for (size_t i = 0; i < frames.size(); ++i) {
    if (frames[i].cmd == nina::Opcode::kSetKey) {
      saw_key = true;
      CHECK(frames[i].params.size() == 3);
      CHECK(frames[i].params[1].size() == 1 && frames[i].params[1][0] == 1);
      CHECK(std::string(frames[i].params[2].begin(),
                        frames[i].params[2].end()) == "abcde");
    }
  }
  CHECK(saw_key);
}

void TestScan() {
  FakeNina sim;
  dash::NinaWifi wifi(sim);
  sim.SetReply(nina::Opcode::kGetFwVersion, {{'1', '.', '4', '.', '8'}});
  CHECK(wifi.Begin());

  sim.SetStatusReply(nina::Opcode::kStartScanNetworks, nina::kAck);
  sim.SetReply(nina::Opcode::kScanNetworks, {{'a', 'l', 'p', 'h', 'a'},
                                             {'b', 'e', 't', 'a'},
                                             {'g', 'a', 'm', 'm', 'a'}});

  sim.SetHandler(
      nina::Opcode::kGetIdxRssi, [](const FakeNina::Frame& f) -> Params {
        const uint8_t index = f.params[0][0];
        const uint8_t rssi = static_cast<uint8_t>(0xCE - index);  // -50 - index
        return Params{{rssi, 0xFF, 0xFF, 0xFF}};
      });
  sim.SetHandler(nina::Opcode::kGetIdxEnct,
                 [](const FakeNina::Frame&) -> Params { return Params{{4}}; });
  sim.SetHandler(
      nina::Opcode::kGetIdxBssid, [](const FakeNina::Frame& f) -> Params {
        const uint8_t index = f.params[0][0];
        return Params{
            {0x02, 0x00, 0x00, 0x00, 0x00, static_cast<uint8_t>(0x10 + index)}};
      });
  sim.SetHandler(nina::Opcode::kGetIdxChannel,
                 [](const FakeNina::Frame& f) -> Params {
                   const uint8_t index = f.params[0][0];
                   return Params{{static_cast<uint8_t>(6 + index)}};
                 });

  dash::ScanEntry entries[5];
  const int count = wifi.Scan(entries, 5);
  CHECK(count == 3);
  CHECK(std::strcmp(entries[0].ssid, "alpha") == 0);
  CHECK(std::strcmp(entries[1].ssid, "beta") == 0);
  CHECK(std::strcmp(entries[2].ssid, "gamma") == 0);
  CHECK(entries[0].rssi == -50);
  CHECK(entries[1].rssi == -51);
  CHECK(entries[2].rssi == -52);
  CHECK(entries[0].encryption == 4);
  CHECK(entries[0].channel == 6);
  CHECK(entries[1].channel == 7);
  CHECK(entries[2].channel == 8);
  CHECK(entries[0].bssid[0] == 0x02);
  CHECK(entries[0].bssid[5] == 0x10);
  CHECK(entries[2].bssid[5] == 0x12);
}

void TestNetworkInfo() {
  FakeNina sim;
  dash::NinaWifi wifi(sim);
  sim.SetReply(nina::Opcode::kGetFwVersion, {{'1', '.', '4', '.', '8'}});
  CHECK(wifi.Begin());

  sim.SetReply(nina::Opcode::kGetIpAddr,
               {{192, 168, 1, 100}, {255, 255, 255, 0}, {192, 168, 1, 1}});
  dash::NetworkAddresses addresses;
  CHECK(wifi.NetworkInfo(addresses));
  CHECK(addresses.ip == dash::Ipv4(192, 168, 1, 100));
  CHECK(addresses.mask == dash::Ipv4(255, 255, 255, 0));
  CHECK(addresses.gateway == dash::Ipv4(192, 168, 1, 1));

  sim.SetReply(nina::Opcode::kGetDnsConfig, {{8, 8, 8, 8}, {1, 1, 1, 1}});
  dash::DnsAddresses dns;
  CHECK(wifi.DnsServers(dns));
  CHECK(dns.primary == dash::Ipv4(8, 8, 8, 8));
  CHECK(dns.secondary == dash::Ipv4(1, 1, 1, 1));
}

void TestStaticIpAndDnsEncoding() {
  FakeNina sim;
  dash::NinaWifi wifi(sim);
  sim.SetReply(nina::Opcode::kGetFwVersion, {{'1', '.', '4', '.', '8'}});
  CHECK(wifi.Begin());

  sim.SetStatusReply(nina::Opcode::kSetIpConfig, nina::kAck);
  CHECK(wifi.SetStaticIp(dash::Ipv4(192, 168, 1, 50),
                         dash::Ipv4(192, 168, 1, 1),
                         dash::Ipv4(255, 255, 255, 0)));

  const FakeNina::Frame& ip_frame = sim.Frames().back();
  CHECK(ip_frame.cmd == nina::Opcode::kSetIpConfig);
  CHECK(ip_frame.params.size() == 4);
  CHECK(ip_frame.params[0].size() == 1 && ip_frame.params[0][0] == 3);
  // The address is sent as a native little-endian uint32_t.
  CHECK(ip_frame.params[1] == std::vector<uint8_t>({50, 1, 168, 192}));
  CHECK(ip_frame.params[2] == std::vector<uint8_t>({1, 1, 168, 192}));
  CHECK(ip_frame.params[3] == std::vector<uint8_t>({0, 255, 255, 255}));

  sim.SetStatusReply(nina::Opcode::kSetDnsConfig, nina::kAck);
  CHECK(wifi.SetDns(dash::Ipv4(8, 8, 4, 4), dash::Ipv4(1, 1, 1, 1)));
  const FakeNina::Frame& dns_frame = sim.Frames().back();
  CHECK(dns_frame.cmd == nina::Opcode::kSetDnsConfig);
  CHECK(dns_frame.params.size() == 3);
  CHECK(dns_frame.params[0].size() == 1 && dns_frame.params[0][0] == 2);
  CHECK(dns_frame.params[1] == std::vector<uint8_t>({4, 4, 8, 8}));
  CHECK(dns_frame.params[2] == std::vector<uint8_t>({1, 1, 1, 1}));

  // With only one DNS server the valid mask must be 1.
  CHECK(wifi.SetDns(dash::Ipv4(9, 9, 9, 9)));
  CHECK(sim.Frames().back().params[0][0] == 1);
}

void TestCurrentLinkInfo() {
  FakeNina sim;
  dash::NinaWifi wifi(sim);
  sim.SetReply(nina::Opcode::kGetFwVersion, {{'1', '.', '4', '.', '8'}});
  CHECK(wifi.Begin());

  sim.SetReply(nina::Opcode::kGetCurrSsid, {{'h', 'o', 'm', 'e'}});
  char ssid[16] = {0};
  CHECK(wifi.CurrentSsid(ssid, sizeof(ssid)));
  CHECK(std::strcmp(ssid, "home") == 0);

  sim.SetReply(nina::Opcode::kGetCurrBssid,
               {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}});
  uint8_t bssid[6] = {0};
  CHECK(wifi.CurrentBssid(bssid));
  CHECK(bssid[0] == 0xAA && bssid[5] == 0xFF);

  sim.SetReply(nina::Opcode::kGetCurrRssi, {{0xC4, 0xFF, 0xFF, 0xFF}});  // -60
  CHECK(wifi.CurrentRssi() == -60);

  sim.SetReply(nina::Opcode::kGetCurrEnct, {{4}});
  CHECK(wifi.CurrentEncryption() == 4);

  sim.SetReply(nina::Opcode::kGetReasonCode, {{205}});
  CHECK(wifi.ReasonCode() == 205);

  sim.SetStatusReply(nina::Opcode::kSetHostname, nina::kAck);
  CHECK(wifi.SetHostname("dash"));
  CHECK(sim.Frames().back().cmd == nina::Opcode::kSetHostname);

  sim.SetStatusReply(nina::Opcode::kDisconnect, nina::kAck);
  CHECK(wifi.Disconnect());
}

void TestResolve() {
  FakeNina sim;
  dash::NinaWifi wifi(sim);
  sim.SetReply(nina::Opcode::kGetFwVersion, {{'1', '.', '4', '.', '8'}});
  CHECK(wifi.Begin());

  sim.SetStatusReply(nina::Opcode::kReqHostByName, nina::kAck);
  sim.SetReply(nina::Opcode::kGetHostByName, {{93, 184, 216, 34}});

  dash::Ipv4 address;
  CHECK(wifi.Resolve("example.com", address));
  CHECK(address == dash::Ipv4(93, 184, 216, 34));

  // The hostname must be in the request frame.
  bool saw_request = false;
  for (size_t i = 0; i < sim.Frames().size(); ++i) {
    if (sim.Frames()[i].cmd == nina::Opcode::kReqHostByName) {
      saw_request = true;
      CHECK(std::string(sim.Frames()[i].params[0].begin(),
                        sim.Frames()[i].params[0].end()) == "example.com");
    }
  }
  CHECK(saw_request);
}

void TestIpv4Literals() {
  dash::Ipv4 address;
  CHECK(dash::ParseIpv4("192.168.1.1", address));
  CHECK(address == dash::Ipv4(192, 168, 1, 1));
  CHECK(dash::ParseIpv4("0.0.0.0", address));
  CHECK(address.IsZero());
  CHECK(dash::ParseIpv4("255.255.255.255", address));

  CHECK(!dash::ParseIpv4("256.1.1.1", address));
  CHECK(!dash::ParseIpv4("1.2.3", address));
  CHECK(!dash::ParseIpv4("1.2.3.4.5", address));
  CHECK(!dash::ParseIpv4("1.2.3.", address));
  CHECK(!dash::ParseIpv4("", address));
  CHECK(!dash::ParseIpv4("abc", address));
  CHECK(!dash::ParseIpv4("1.2.3.4 ", address));
}

}  // namespace

int main() {
  TestBeginReadsFirmware();
  TestConnectWpa();
  TestConnectFailure();
  TestConnectOpenAndWep();
  TestScan();
  TestNetworkInfo();
  TestStaticIpAndDnsEncoding();
  TestCurrentLinkInfo();
  TestResolve();
  TestIpv4Literals();

  if (g_failures == 0) {
    std::printf("all station tests passed\n");
    return 0;
  }
  std::printf("%d check(s) failed\n", g_failures);
  return 1;
}
