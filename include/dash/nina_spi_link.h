// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#ifndef DASHNET_DASH_NINA_SPI_LINK_H_
#define DASHNET_DASH_NINA_SPI_LINK_H_

#include "dash/nina_link.h"

namespace dash {

// Board wiring of the NINA-W102 on the MKR WiFi 1010.
struct NinaPins {
  uint8_t cs;     // NINA_CS      D28 / PA14
  uint8_t ack;    // NINA_ACK     D35 / PA28
  uint8_t reset;  // NINA_RESETN  D31 / PB08, active low
  uint8_t gpio0;  // NINA_GPIO0   D30 / PA27, data-ready / boot strap
};

class NinaSpiLink : public NinaLink {
 public:
  static constexpr NinaPins kDefaultPins = {28, 35, 31, 30};
  static constexpr uint32_t kDefaultClockHz = 8000000;

  explicit NinaSpiLink(const NinaPins& pins = kDefaultPins,
                       uint32_t clock_hz = kDefaultClockHz);

  bool Begin() override;
  void End() override;

  bool WaitReady(uint32_t timeout_ms) override;
  bool Select(uint32_t timeout_ms) override;
  void Deselect() override;
  uint8_t Transfer(uint8_t data) override;
  bool DataReady() override;
  uint32_t Millis() override;
  void DelayMs(uint32_t ms) override;

 private:
  NinaPins pins_;
  uint32_t clock_hz_;
  bool selected_ = false;
};

}  // namespace dash

#endif  // DASHNET_DASH_NINA_SPI_LINK_H_
