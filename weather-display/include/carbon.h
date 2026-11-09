/**
 * @file    carbon.h
 * @brief   GB electricity carbon intensity for the local region (National Grid ESO / NESO).
 *
 * @details API: https://api.carbonintensity.org.uk (free, no key). Uses the
 *          regional endpoints for CARBON_REGION_ID (3 = North West England):
 *          - /regional/regionid/{id}                            current intensity + generation mix
 *          - /regional/intensity/{from}/fw48h/regionid/{id}     48 h forecast, half-hourly
 *          Intensity is in gCO2/kWh.
 */
#pragma once

#include <Arduino.h>

/** @brief Official intensity bands. */
enum class CarbonIndex : uint8_t { Unknown, VeryLow, Low, Moderate, High, VeryHigh };

/** @brief Generation fuels in the order the mix is stored. */
enum class Fuel : uint8_t { Wind, Solar, Nuclear, Hydro, Biomass, Imports, Gas, Coal, Other, Count };

/** @brief Half-hour forecast slots kept (48 h). */
constexpr uint8_t CARBON_SLOTS = 96;

/** @brief One half-hour forecast slot. */
struct CarbonSlot {
  uint32_t from = 0;       ///< Start of the half hour (UTC epoch).
  uint16_t gPerKwh = 0;    ///< Forecast intensity.
  CarbonIndex index = CarbonIndex::Unknown;
};

/** @brief Current regional intensity, mix and 48 h forecast. */
struct CarbonData {
  bool valid = false;                             ///< False until the first successful fetch.
  uint32_t fetchedAtMs = 0;                       ///< millis() at fetch time.
  uint16_t nowGPerKwh = 0;                        ///< Current half hour.
  CarbonIndex nowIndex = CarbonIndex::Unknown;
  float mix[static_cast<int>(Fuel::Count)] = {};  ///< Generation mix now, % by fuel.
  CarbonSlot forecast[CARBON_SLOTS];              ///< From the current half hour.
  uint8_t count = 0;                              ///< Valid forecast slots.

  /**
   * @brief  Find the lowest-carbon window of @p slots consecutive half hours.
   * @param  slots        Window length in half hours (e.g. 6 = 3 h).
   * @param  withinSlots  Only consider windows starting within this many slots.
   * @return Index of the first slot of the window, or -1.
   */
  int greenestWindow(uint8_t slots, uint8_t withinSlots) const;
};

namespace Carbon {

/** @brief Download the current mix and the 48 h forecast. */
bool fetch(CarbonData &out);

/** @brief "Very low" ... "Very high". */
const char *indexName(CarbonIndex i);

/** @brief "Wind", "Solar", ... */
const char *fuelName(Fuel f);

}  // namespace Carbon
