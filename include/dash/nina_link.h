// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#ifndef DASHNET_DASH_NINA_LINK_H_
#define DASHNET_DASH_NINA_LINK_H_

#include <stdint.h>

namespace dash {

// Byte transport between the host and the NINA module.
//
// The frame layer only needs these seven operations, which keeps the protocol
// logic independent from the SAMD21 hardware and testable on a host machine.
class NinaLink {
 public:
  virtual ~NinaLink() {}

  // Initialise the bus and reset the module. Returns false if the module did
  // not come up.
  virtual bool Begin() = 0;
  virtual void End() = 0;

  // Block until the module is ready to start a transaction (ACK low).
  virtual bool WaitReady(uint32_t timeout_ms) = 0;

  // Assert chip select and wait for the module acknowledge (ACK high).
  virtual bool Select(uint32_t timeout_ms) = 0;

  // Deassert chip select.
  virtual void Deselect() = 0;

  // Full duplex transfer of one byte.
  virtual uint8_t Transfer(uint8_t data) = 0;

  // True when the module has inbound socket data pending (GPIO0 high).
  virtual bool DataReady() = 0;

  virtual uint32_t Millis() = 0;
  virtual void DelayMs(uint32_t ms) = 0;
};

}  // namespace dash

#endif  // DASHNET_DASH_NINA_LINK_H_
