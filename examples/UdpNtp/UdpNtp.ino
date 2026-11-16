// DashNet example: SNTP time query over UDP.
//
// Compile with:
//   arduino-cli compile -b arduino:samd:mkrwifi1010 --library . examples/UdpNtp

#include <dash_net.h>

#define WIFI_SSID "your-ssid"
#define WIFI_PASSPHRASE "your-passphrase"
#define NTP_HOST "pool.ntp.org"

dash::Net net;
dash::UdpSocket udp(net);

// Seconds between the NTP epoch (1900) and the Unix epoch (1970).
constexpr uint32_t kNtpUnixDelta = 2208988800UL;

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
    Serial.print(F("join failed, reason "));
    Serial.println(net.ReasonCode());
    return;
  }

  if (!udp.BeginPacket(NTP_HOST, 123)) {
    Serial.println(F("packet setup failed"));
    return;
  }

  uint8_t request[48] = {0};
  request[0] = 0x23;  // LI 0, version 4, mode 3 (client)
  if (udp.Write(request, sizeof(request)) !=
      static_cast<int>(sizeof(request))) {
    Serial.println(F("write failed"));
    return;
  }
  if (!udp.EndPacket()) {
    Serial.println(F("send failed"));
    return;
  }

  uint8_t reply[48];
  const uint32_t start = millis();
  while (millis() - start < 5000) {
    const int size = udp.ParsePacket();
    if (size >= static_cast<int>(sizeof(reply))) {
      udp.Read(reply, sizeof(reply));
      const uint32_t seconds = (static_cast<uint32_t>(reply[40]) << 24) |
                               (static_cast<uint32_t>(reply[41]) << 16) |
                               (static_cast<uint32_t>(reply[42]) << 8) |
                               reply[43];
      Serial.print(F("unix time "));
      Serial.println(seconds - kNtpUnixDelta);
      udp.Close();
      return;
    }
    delay(10);
  }
  Serial.println(F("no reply"));
  udp.Close();
}

void loop() {}
