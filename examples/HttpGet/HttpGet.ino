// DashNet example: plain HTTP GET over TCP.
//
// Compile with:
//   arduino-cli compile -b arduino:samd:mkrwifi1010 --library .
//   examples/HttpGet

#include <dash_net.h>

#define WIFI_SSID "your-ssid"
#define WIFI_PASSPHRASE "your-passphrase"

dash::Net net;

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
  Serial.println(F("connected"));

  dash::TcpSocket client(net);
  if (!client.Connect("example.com", 80, 10000)) {
    Serial.println(F("connect failed"));
    return;
  }
  Serial.println(F("tcp connected"));

  const char request[] =
      "GET / HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "Connection: close\r\n"
      "\r\n";
  if (client.Write(request, sizeof(request) - 1) <= 0) {
    Serial.println(F("write failed"));
    client.Close();
    return;
  }
  client.Flush();

  char buffer[129];
  const uint32_t start = millis();
  while (millis() - start < 15000) {
    const int got = client.Read(buffer, sizeof(buffer) - 1);
    if (got > 0) {
      buffer[got] = '\0';
      Serial.print(buffer);
    } else if (!client.Connected()) {
      break;
    } else {
      delay(10);
    }
  }
  Serial.println();
  Serial.println(F("--- done ---"));
  client.Close();
}

void loop() {}
