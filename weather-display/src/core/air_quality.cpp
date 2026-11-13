/**
 * @file    air_quality.cpp
 * @brief   Open-Meteo Air Quality API client.
 */
#include "air_quality.h"
#include "log.h"

#include <ArduinoJson.h>

#include "config.h"
#include "https_get.h"

namespace {

/** @brief Request URL: current values only. */
const char *const URL =
    "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" WX_LATITUDE "&longitude=" WX_LONGITUDE
    "&current=european_aqi,pm2_5,pm10,nitrogen_dioxide,ozone,uv_index,grass_pollen,birch_pollen,alder_pollen"
    "&timezone=" WX_TIMEZONE;

}  // namespace

namespace AirQuality {

bool fetch(AirQualityData &out) {
  JsonDocument doc;
  const bool ok = Https::get(URL, "air", [&](HTTPClient &http) {
    const DeserializationError err = deserializeJson(doc, http.getStream());
    if (err) Log::printf("[air] JSON error: %s\n", err.c_str());
    return !err;
  });
  if (!ok) return false;

  JsonObject c = doc["current"];
  if (c.isNull()) return false;

  AirQualityData aq;
  aq.eaqi = c["european_aqi"] | 0;
  aq.pm25 = c["pm2_5"] | 0.0f;
  aq.pm10 = c["pm10"] | 0.0f;
  aq.no2 = c["nitrogen_dioxide"] | 0.0f;
  aq.o3 = c["ozone"] | 0.0f;
  aq.uv = c["uv_index"] | 0.0f;
  aq.grass = c["grass_pollen"] | 0.0f;
  aq.birch = c["birch_pollen"] | 0.0f;
  aq.alder = c["alder_pollen"] | 0.0f;
  aq.valid = true;
  aq.fetchedAtMs = millis();
  out = aq;

  const char *ptype = nullptr;
  const char *plevel = pollen(aq, ptype);
  Log::printf("[air] EAQI %d (%s), PM2.5 %.1f, PM10 %.1f, NO2 %.1f, O3 %.1f, UV %.1f, pollen %s%s%s\n", aq.eaqi,
                band(aq.eaqi), aq.pm25, aq.pm10, aq.no2, aq.o3, aq.uv, plevel, ptype ? " " : "", ptype ? ptype : "");
  return true;
}

uint8_t bandIndex(int eaqi) {
  if (eaqi < 20) return 0;
  if (eaqi < 40) return 1;
  if (eaqi < 60) return 2;
  if (eaqi < 80) return 3;
  if (eaqi < 100) return 4;
  return 5;
}

const char *band(int eaqi) {
  static const char *const names[] = {"Good", "Fair", "Moderate", "Poor", "Very poor", "Extreme"};
  return names[bandIndex(eaqi)];
}

const char *uvBand(float uv) {
  if (uv < 3) return "Low";
  if (uv < 6) return "Moderate";
  if (uv < 8) return "High";
  if (uv < 11) return "Very high";
  return "Extreme";
}

const char *pollen(const AirQualityData &aq, const char *&type) {
  // Pick the pollen type with the highest count.
  float v = aq.grass;
  type = "Grass";
  if (aq.birch > v) { v = aq.birch; type = "Birch"; }
  if (aq.alder > v) { v = aq.alder; type = "Alder"; }

  // Indicative thresholds in grains/m3 (UK-style Low/Moderate/High/Very high).
  if (v < 1) { type = nullptr; return "None"; }
  if (v < 30) return "Low";
  if (v < 50) return "Moderate";
  if (v < 150) return "High";
  return "Very high";
}

}  // namespace AirQuality
