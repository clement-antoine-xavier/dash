// DashNet example: scan for networks.
//
// Compile with:
//   arduino-cli compile -b arduino:samd:mkrwifi1010 --library . examples/Scan

#include <dash_net.h>

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
}

void setup() {
  Serial.begin(115200);
  const uint32_t wait_start = millis();
  while (!Serial && (millis() - wait_start) < 3000) {
  }

  Serial.print(F("DashNet "));
  Serial.println(dash::Version());

  if (!net.Begin()) {
    Serial.println(F("NINA bring-up failed"));
    return;
  }
  Serial.print(F("firmware "));
  Serial.println(net.FirmwareVersion());

  dash::ScanEntry entries[10];
  const int count = net.Scan(entries, 10);
  if (count < 0) {
    Serial.println(F("scan failed"));
    return;
  }

  Serial.print(count);
  Serial.println(F(" network(s):"));
  for (int i = 0; i < count; ++i) {
    Serial.print(F("  "));
    Serial.print(entries[i].rssi);
    Serial.print(F(" dBm  ch "));
    Serial.print(entries[i].channel);
    Serial.print(F("  enc "));
    Serial.print(entries[i].encryption);
    Serial.print(F("  "));
    Serial.print(entries[i].ssid);
    Serial.print(F("  "));
    PrintMac(entries[i].bssid);
    Serial.println();
  }
}

void loop() {}
