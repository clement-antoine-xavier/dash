// DashNet example: join a WPA2 network and print the connection details.
//
// Replace the credentials below, then compile with:
//   arduino-cli compile -b arduino:samd:mkrwifi1010 --library .
//   examples/Connect

#include <dash_net.h>

#define WIFI_SSID "your-ssid"
#define WIFI_PASSPHRASE "your-passphrase"

dash::Net net;

void PrintAddress(const char* label, const dash::Ipv4& address) {
  char buffer[16];
  address.ToString(buffer, sizeof(buffer));
  Serial.print(label);
  Serial.println(buffer);
}

void setup() {
  Serial.begin(115200);
  const uint32_t wait_start = millis();
  while (!Serial && (millis() - wait_start) < 3000) {
  }

  if (!net.Begin()) {
    Serial.println(F("NINA bring-up failed"));
    return;
  }
  Serial.print(F("firmware "));
  Serial.println(net.FirmwareVersion());

  Serial.print(F("joining "));
  Serial.println(WIFI_SSID);
  if (!net.Connect(WIFI_SSID, WIFI_PASSPHRASE, 30000)) {
    Serial.print(F("join failed, status "));
    Serial.print(static_cast<uint8_t>(net.ConnectionStatus()));
    Serial.print(F(", reason "));
    Serial.println(net.ReasonCode());
    return;
  }

  Serial.println(F("connected"));

  dash::NetworkAddresses addresses;
  if (net.NetworkInfo(addresses)) {
    PrintAddress("ip      ", addresses.ip);
    PrintAddress("mask    ", addresses.mask);
    PrintAddress("gateway ", addresses.gateway);
  }

  dash::DnsAddresses dns;
  if (net.DnsServers(dns)) {
    PrintAddress("dns     ", dns.primary);
    PrintAddress("dns2    ", dns.secondary);
  }

  char ssid[33];
  if (net.CurrentSsid(ssid, sizeof(ssid))) {
    Serial.print(F("ssid    "));
    Serial.println(ssid);
  }

  Serial.print(F("rssi    "));
  Serial.print(net.Rssi());
  Serial.println(F(" dBm"));
}

void loop() {
  delay(10000);
  Serial.print(F("status  "));
  Serial.println(static_cast<uint8_t>(net.ConnectionStatus()));
}
