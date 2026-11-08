/**
 * @file    battery.cpp
 * @brief   Implementation of the fuel-gauge wrapper.
 */
#include "battery.h"

#include <Adafruit_LC709203F.h>
#include <Adafruit_MAX1704X.h>

namespace {

/** @brief Supported gauge chips. */
enum class Gauge { None, Max17048, Lc709203f };

Adafruit_MAX17048 maxGauge;   ///< Newer board revisions (I2C 0x36).
Adafruit_LC709203F lcGauge;   ///< Older board revisions (I2C 0x0B).
Gauge gauge = Gauge::None;    ///< Gauge detected by Battery::begin().
BatteryStatus state;          ///< Cached reading.

/** @brief Voltage window outside which we assume no LiPo is connected. */
constexpr float MIN_PLAUSIBLE_V = 3.0f;
constexpr float MAX_PLAUSIBLE_V = 4.5f;

}  // namespace

namespace Battery {

bool begin(TwoWire &wire) {
  if (maxGauge.begin(&wire)) {
    gauge = Gauge::Max17048;
    state.gaugeName = "MAX17048";
    state.rateSupported = true;
  } else if (lcGauge.begin(&wire)) {
    gauge = Gauge::Lc709203f;
    lcGauge.setPackSize(LC709203F_APA_500MAH);
    state.gaugeName = "LC709203F";
    state.rateSupported = false;
  }
  state.gaugeFound = gauge != Gauge::None;
  Serial.printf("[battery] gauge: %s\n", state.gaugeName);
  return state.gaugeFound;
}

void update() {
  switch (gauge) {
    case Gauge::Max17048:
      state.voltage = maxGauge.cellVoltage();
      state.percent = maxGauge.cellPercent();
      state.chargeRate = maxGauge.chargeRate();
      break;
    case Gauge::Lc709203f:
      state.voltage = lcGauge.cellVoltage();
      state.percent = lcGauge.cellPercent();
      state.chargeRate = 0.0f;
      break;
    case Gauge::None:
      return;
  }
  // With no LiPo attached the gauge reports nonsense, so sanity-check it.
  state.present = !isnan(state.voltage) && state.voltage > MIN_PLAUSIBLE_V && state.voltage < MAX_PLAUSIBLE_V;
  state.percent = constrain(state.percent, 0.0f, 100.0f);
}

const BatteryStatus &status() { return state; }

}  // namespace Battery
