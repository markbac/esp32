/**
 * @file    battery.h
 * @brief   LiPo fuel-gauge driver wrapper (MAX17048 or LC709203F).
 *
 * @details Adafruit has shipped the ESP32-S3 TFT Feather with two different
 *          I2C fuel gauges. This module probes for both and exposes a single
 *          read-only @ref BatteryStatus snapshot to the rest of the app.
 */
#pragma once

#include <Arduino.h>
#include <Wire.h>

/** @brief Point-in-time battery reading. */
struct BatteryStatus {
  const char *gaugeName = "none";  ///< Detected gauge chip, or "none".
  bool gaugeFound = false;         ///< True if a fuel gauge responded on I2C.
  bool present = false;            ///< True if a plausible LiPo voltage is measured.
  bool rateSupported = false;      ///< True if @ref chargeRate is meaningful.
  float voltage = 0.0f;            ///< Cell voltage in volts.
  float percent = 0.0f;            ///< State of charge, 0-100 %.
  float chargeRate = 0.0f;         ///< Charge (+) / discharge (-) rate in %/hour.

  /** @brief Heuristic: battery present and gaining charge. */
  bool charging() const { return present && rateSupported && chargeRate > 0.5f; }
};

namespace Battery {

/**
 * @brief  Probe the I2C bus for a supported fuel gauge.
 * @param  wire  I2C bus the gauge is attached to (already started).
 * @return true if a gauge was found.
 */
bool begin(TwoWire &wire);

/** @brief Read the gauge and refresh the cached @ref BatteryStatus. */
void update();

/** @brief Latest cached reading. */
const BatteryStatus &status();

}  // namespace Battery
