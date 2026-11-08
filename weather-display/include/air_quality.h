/**
 * @file    air_quality.h
 * @brief   Air quality, UV and pollen from the Open-Meteo Air Quality API.
 *
 * @details Uses the European Air Quality Index (EAQI). Open-Meteo does not
 *          provide the UK DAQI. Pollen is modelled for Europe only and reads
 *          zero out of season.
 */
#pragma once

#include <Arduino.h>

/** @brief Latest air-quality snapshot. */
struct AirQualityData {
  bool valid = false;        ///< False until the first successful fetch.
  uint32_t fetchedAtMs = 0;  ///< millis() at fetch time.
  int eaqi = 0;              ///< European AQI (0 = best, 100+ = extremely poor).
  float pm25 = 0;            ///< PM2.5, ug/m3.
  float pm10 = 0;            ///< PM10, ug/m3.
  float no2 = 0;             ///< Nitrogen dioxide, ug/m3.
  float o3 = 0;              ///< Ozone, ug/m3.
  float uv = 0;              ///< Current UV index.
  float grass = 0;           ///< Grass pollen, grains/m3.
  float birch = 0;           ///< Birch pollen, grains/m3.
  float alder = 0;           ///< Alder pollen, grains/m3.
};

namespace AirQuality {

/**
 * @brief  Download current air quality for the configured location.
 * @param[out] out  Overwritten only on success.
 * @return true on success.
 */
bool fetch(AirQualityData &out);

/** @brief EAQI band name: Good, Fair, Moderate, Poor, Very poor, Extreme. */
const char *band(int eaqi);

/** @brief EAQI band index 0 (Good) ... 5 (Extremely poor). */
uint8_t bandIndex(int eaqi);

/** @brief WHO UV category: Low, Moderate, High, Very high, Extreme. */
const char *uvBand(float uv);

/**
 * @brief  Dominant pollen type and its level.
 * @param  aq        Snapshot to inspect.
 * @param[out] type  "Grass", "Birch" or "Alder" (nullptr when none).
 * @return "None", "Low", "Moderate", "High" or "Very high".
 */
const char *pollen(const AirQualityData &aq, const char *&type);

}  // namespace AirQuality
