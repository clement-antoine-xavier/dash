// DashNet example: minimal TCP echo server.
//
// Compile with:
//   arduino-cli compile -b arduino:samd:mkrwifi1010 --library .
//   examples/TcpServer

#include <dash_net.h>

#define WIFI_SSID "your-ssid"
#define WIFI_PASSPHRASE "your-passphrase"

dash::Net net;
dash::TcpServer server(net);

void setup() {
  Serial.begin(115200);
  const uint32_t wait_start = millis();
  while (!Serial && (millis() - wait_start) < 3000) {
  }

  if (!net.Begin()) {
    Serial.println(F("NINA bring-up failed"));
    return;
  }
  if (!net.Connect(WIFI_SSID, WIFI_PASSPHRASE, 30000)) {
    const uint8_t reason = net.ReasonCode();
    Serial.print(F("join failed, reason "));
    Serial.print(reason);
    Serial.print(F(" ("));
    Serial.print(dash::ReasonName(reason));
    Serial.println(')');
    return;
  }

  if (!server.Begin(5000)) {
    Serial.println(F("listen failed"));
    return;
  }

  dash::NetworkAddresses addresses;
  char buffer[16];
  if (net.NetworkInfo(addresses)) {
    addresses.ip.ToString(buffer, sizeof(buffer));
    Serial.print(F("listening on "));
    Serial.print(buffer);
    Serial.println(F(":5000"));
  }
}

void loop() {
  dash::TcpSocket client(net);
  if (!server.Accept(client)) {
    delay(100);
    return;
  }
  Serial.println(F("client connected"));

  char buffer[128];
  const uint32_t start = millis();
  while (millis() - start < 30000 && client.Connected()) {
    const int got = client.Read(buffer, sizeof(buffer));
    if (got > 0) {
      client.Write(buffer, static_cast<size_t>(got));
      client.Flush();
    } else {
      delay(10);
    }
  }

  client.Close();
  Serial.println(F("client closed"));
}
