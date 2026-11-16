// DashNet - from-scratch WiFi driver for the Arduino MKR WiFi 1010.
// SPDX-License-Identifier: MIT

#include "dash/nina_spi_link.h"

#include <Arduino.h>
#include <SPI.h>

namespace dash {

constexpr NinaPins NinaSpiLink::kDefaultPins;
constexpr uint32_t NinaSpiLink::kDefaultClockHz;

namespace {

// The module needs about 750 ms after reset before it accepts commands.
constexpr uint32_t kResetHoldMs = 10;
constexpr uint32_t kBootDelayMs = 750;
constexpr uint32_t kBootReadyTimeoutMs = 3000;

}  // namespace

NinaSpiLink::NinaSpiLink(const NinaPins& pins, uint32_t clock_hz)
    : pins_(pins), clock_hz_(clock_hz) {}

bool NinaSpiLink::Begin() {
  pinMode(pins_.cs, OUTPUT);
  pinMode(pins_.ack, INPUT);
  pinMode(pins_.reset, OUTPUT);
  pinMode(pins_.gpio0, OUTPUT);

  // Keep GPIO0 high across the reset so the ESP32 does not enter its serial
  // bootloader, then release the line once the firmware is running.
  digitalWrite(pins_.gpio0, HIGH);
  digitalWrite(pins_.cs, HIGH);
  digitalWrite(pins_.reset, HIGH);  // assert reset (active low)
  delay(kResetHoldMs);
  digitalWrite(pins_.reset, LOW);  // release reset
  delay(kBootDelayMs);
  digitalWrite(pins_.gpio0, LOW);
  pinMode(pins_.gpio0, INPUT);

  SPI1.begin();
  selected_ = false;

  return WaitReady(kBootReadyTimeoutMs);
}

void NinaSpiLink::End() {
  digitalWrite(pins_.reset, HIGH);  // hold the module in reset
  digitalWrite(pins_.cs, HIGH);
  pinMode(pins_.cs, INPUT);
  SPI1.end();
  selected_ = false;
}

bool NinaSpiLink::WaitReady(uint32_t timeout_ms) {
  const uint32_t start = ::millis();
  while (digitalRead(pins_.ack) != LOW) {
    if (static_cast<uint32_t>(::millis() - start) >= timeout_ms) {
      return false;
    }
  }
  return true;
}

bool NinaSpiLink::Select(uint32_t timeout_ms) {
  SPI1.beginTransaction(SPISettings(clock_hz_, MSBFIRST, SPI_MODE0));
  digitalWrite(pins_.cs, LOW);
  selected_ = true;

  // The module raises ACK to acknowledge the chip select. As in the reference
  // implementation the wait is advisory: a slow module must not be treated as
  // absent here, the reply parsing will fail cleanly if it is.
  const uint32_t start = ::millis();
  while (digitalRead(pins_.ack) != HIGH) {
    if (static_cast<uint32_t>(::millis() - start) >= timeout_ms) {
      break;
    }
  }
  return true;
}

void NinaSpiLink::Deselect() {
  digitalWrite(pins_.cs, HIGH);
  if (selected_) {
    SPI1.endTransaction();
    selected_ = false;
  }
}

uint8_t NinaSpiLink::Transfer(uint8_t data) { return SPI1.transfer(data); }

bool NinaSpiLink::DataReady() { return digitalRead(pins_.gpio0) == HIGH; }

uint32_t NinaSpiLink::Millis() { return ::millis(); }

void NinaSpiLink::DelayMs(uint32_t ms) { delay(ms); }

}  // namespace dash
