/**
 * @file    frost.h
 * @brief   Frost and ice risk from Tempest readings and the hourly forecast.
 *
 * @details Rules (first match wins):
 *          | Condition                                             | Level    |
 *          |-------------------------------------------------------|----------|
 *          | Air <= 0 C and surfaces wet (rain in the last hour/day)| High (ice) |
 *          | Air <= 0 C                                             | High (frost) |
 *          | Air <= 2 C and surfaces wet                            | High (ice risk) |
 *          | Air <= 3 C and dew point <= 0 C                        | Moderate (ground frost) |
 *          | Forecast minimum (next 12 h) <= 0 C                    | Moderate (frost expected) |
 *          | Forecast minimum (next 12 h) <= 3 C                    | Low (ground frost possible) |
 *          Ground and car surfaces cool below the 2 m air temperature on clear
 *          nights, hence the 3 C margins.
 */
#pragma once

#include <Arduino.h>

#include "tempest.h"
#include "weather.h"

/** @brief Overall risk level. */
enum class FrostLevel : uint8_t { None, Low, Moderate, High };

/** @brief Assessment result. */
struct FrostRisk {
  FrostLevel level = FrostLevel::None;
  char headline[40] = "No frost risk";   ///< Short summary.
  char detail[64] = "";                  ///< Supporting numbers.
  float forecastMin = NAN;               ///< Lowest forecast temperature in the next 12 h.
  uint8_t forecastMinHour = 0;           ///< Local hour of that minimum.
};

namespace Frost {

/** @brief Assess the risk now (caller holds the data lock for the inputs). */
FrostRisk assess(const WeatherData &wx, const TempestData &tempest);

/** @brief "None", "Low", "Moderate", "High". */
const char *levelName(FrostLevel l);

}  // namespace Frost
