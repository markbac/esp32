/**
 * @file    status_led.cpp
 * @brief   NeoPixel alert-light implementation.
 */
#include "status_led.h"

#include <Arduino.h>

#include "config.h"

namespace {

uint8_t lastR = 255, lastG = 255, lastB = 255;  ///< Last colour written (avoid redundant writes).

/** @brief Write a colour only if it changed. */
void setColor(uint8_t r, uint8_t g, uint8_t b) {
  if (r == lastR && g == lastG && b == lastB) return;
  rgbLedWrite(PIN_NEOPIXEL, r, g, b);
  lastR = r;
  lastG = g;
  lastB = b;
}

/** @brief Scale an 8-bit channel by LED_MAX_BRIGHTNESS / 255. */
uint8_t dim(uint8_t v, float k = 1.0f) { return static_cast<uint8_t>(v * k * LED_MAX_BRIGHTNESS / 255.0f); }

}  // namespace

namespace StatusLed {

void begin() {
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
  setColor(0, 0, 0);
}

void update(WarningLevel warning, AuroraLevel aurora) {
  if (!LED_ENABLED) return setColor(0, 0, 0);

  if (warning == WarningLevel::Red) return setColor(dim(255), 0, 0);
  if (warning == WarningLevel::Amber) return setColor(dim(255), dim(100), 0);

  if (aurora == AuroraLevel::Red || aurora == AuroraLevel::Amber) {
    // 4-second breathing cycle; red alerts pulse brighter.
    const float phase = (millis() % 4000) / 4000.0f;
    const float k = 0.15f + 0.85f * (0.5f - 0.5f * cosf(2.0f * PI * phase));
    const float peak = aurora == AuroraLevel::Red ? 1.0f : 0.6f;
    return setColor(dim(40, k * peak), dim(255, k * peak), dim(90, k * peak));
  }
  setColor(0, 0, 0);
}

}  // namespace StatusLed
