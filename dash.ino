// DashNet bring-up sketch for the Arduino MKR WiFi 1010.
//
// Resets the NINA-W102, performs the SPI handshake and prints the module
// identity. This is the first hardware checkpoint: if it prints a firmware
// version and a MAC address, the transport layer works.

#include <Arduino.h>

#include "src/dash_net.h"

namespace {

dash::Net net;

void PrintMac(const uint8_t* mac) {
  for (uint8_t i = 0; i < 6; ++i) {
    if (mac[i] < 0x10) {
      Serial.print('0');
    }
    Serial.print(mac[i], HEX);
    if (i != 5) {
      Serial.print(':');
    }
  }
  Serial.println();
}

const char* StatusName(dash::Status status) {
  switch (status) {
    case dash::Status::kNoModule:
      return "no module";
    case dash::Status::kIdle:
      return "idle";
    case dash::Status::kNoSsid:
      return "no SSID";
    case dash::Status::kScanDone:
      return "scan done";
    case dash::Status::kConnected:
      return "connected";
    case dash::Status::kConnectFailed:
      return "connect failed";
    case dash::Status::kConnectionLost:
      return "connection lost";
    case dash::Status::kDisconnected:
      return "disconnected";
    case dash::Status::kApListening:
      return "AP listening";
    case dash::Status::kApConnected:
      return "AP connected";
    case dash::Status::kApFailed:
      return "AP failed";
    default:
      return "unknown";
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t wait_start = millis();
  while (!Serial && (millis() - wait_start) < 3000) {
  }

  Serial.println();
  Serial.print(F("DashNet "));
  Serial.println(dash::Version());
  Serial.println(F("resetting NINA-W102..."));

  const uint32_t started = millis();
  if (!net.Begin()) {
    Serial.print(F("FAILED after "));
    Serial.print(millis() - started);
    Serial.println(F(" ms"));
    Serial.println(F("check the module firmware and the SPI wiring"));
    return;
  }

  Serial.print(F("module ready in "));
  Serial.print(millis() - started);
  Serial.println(F(" ms"));

  Serial.print(F("firmware: "));
  Serial.println(net.FirmwareVersion());

  Serial.print(F("firmware id: 0x"));
  Serial.println(net.FirmwareVersionU32(), HEX);

  uint8_t mac[6] = {0};
  if (net.Mac(mac)) {
    Serial.print(F("mac: "));
    PrintMac(mac);
  } else {
    Serial.println(F("mac: read failed"));
  }

  Serial.print(F("status: "));
  Serial.println(StatusName(net.ConnectionStatus()));
  Serial.print(F("reason: "));
  Serial.println(net.ReasonCode());
}

void loop() {
  delay(5000);

  Serial.print(F("status: "));
  Serial.println(StatusName(net.ConnectionStatus()));
}
