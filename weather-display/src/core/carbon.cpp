/**
 * @file    carbon.cpp
 * @brief   Carbon Intensity API client.
 */
#include "carbon.h"

#include <ArduinoJson.h>
#include <time.h>

#include "config.h"
#include "https_get.h"

namespace {

/** @brief Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm). */
int32_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

/** @brief Parse "2026-11-08T19:30Z" (UTC) to an epoch. */
uint32_t parseUtc(const char *s) {
  int y, mo, d, h, mi;
  if (!s || sscanf(s, "%d-%d-%dT%d:%dZ", &y, &mo, &d, &h, &mi) != 5) return 0;
  return static_cast<uint32_t>(daysFromCivil(y, mo, d)) * 86400UL + h * 3600UL + mi * 60UL;
}

CarbonIndex parseIndex(const char *s) {
  if (!s) return CarbonIndex::Unknown;
  if (!strcmp(s, "very low")) return CarbonIndex::VeryLow;
  if (!strcmp(s, "low")) return CarbonIndex::Low;
  if (!strcmp(s, "moderate")) return CarbonIndex::Moderate;
  if (!strcmp(s, "high")) return CarbonIndex::High;
  if (!strcmp(s, "very high")) return CarbonIndex::VeryHigh;
  return CarbonIndex::Unknown;
}

int fuelIndex(const char *f) {
  static const char *const names[] = {"wind", "solar", "nuclear", "hydro", "biomass", "imports", "gas", "coal", "other"};
  for (int i = 0; i < static_cast<int>(Fuel::Count); ++i) {
    if (!strcmp(f, names[i])) return i;
  }
  return -1;
}

/** @brief GET JSON with a filter (the API sends chunked HTTP/1.1, so parse the full body). */
bool getJson(const String &url, JsonDocument &doc, JsonDocument &filter) {
  return Https::get(
      url, "carbon",
      [&](HTTPClient &http) { return !deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter)); },
      false);
}

}  // namespace

int CarbonData::greenestWindow(uint8_t slots, uint8_t withinSlots) const {
  int best = -1;
  uint32_t bestSum = UINT32_MAX;
  for (int i = 0; i + slots <= count && i < withinSlots; ++i) {
    uint32_t sum = 0;
    for (int k = 0; k < slots; ++k) sum += forecast[i + k].gPerKwh;
    if (sum < bestSum) {
      bestSum = sum;
      best = i;
    }
  }
  return best;
}

namespace Carbon {

bool fetch(CarbonData &out) {
  CarbonData c;

  // Current half hour with generation mix.
  {
    JsonDocument filter;
    JsonObject f = filter["data"][0]["data"][0].to<JsonObject>();
    f["intensity"] = true;
    f["generationmix"] = true;
    JsonDocument doc;
    const String url = String("https://api.carbonintensity.org.uk/regional/regionid/") + CARBON_REGION_ID;
    if (!getJson(url, doc, filter)) return false;
    JsonObject cur = doc["data"][0]["data"][0];
    if (cur.isNull()) return false;
    c.nowGPerKwh = cur["intensity"]["forecast"] | 0;
    c.nowIndex = parseIndex(cur["intensity"]["index"] | "");
    JsonArray mixArr = cur["generationmix"];
    for (JsonObject m : mixArr) {
      const int i = fuelIndex(m["fuel"] | "");
      if (i >= 0) c.mix[i] = m["perc"] | 0.0f;
    }
  }

  // 48 h forecast, intensity only.
  {
    char from[24];
    const time_t now = time(nullptr);
    tm utc;
    gmtime_r(&now, &utc);
    strftime(from, sizeof(from), "%Y-%m-%dT%H:%MZ", &utc);
    JsonDocument filter;
    JsonObject f = filter["data"]["data"][0].to<JsonObject>();
    f["from"] = true;
    f["intensity"] = true;
    JsonDocument doc;
    const String url = String("https://api.carbonintensity.org.uk/regional/intensity/") + from + "/fw48h/regionid/" +
                       CARBON_REGION_ID;
    if (getJson(url, doc, filter)) {
      JsonArray slots = doc["data"]["data"];
      for (JsonObject s : slots) {
        if (c.count >= CARBON_SLOTS) break;
        CarbonSlot &slot = c.forecast[c.count++];
        slot.from = parseUtc(s["from"] | "");
        slot.gPerKwh = s["intensity"]["forecast"] | 0;
        slot.index = parseIndex(s["intensity"]["index"] | "");
      }
    }
  }

  c.valid = true;
  c.fetchedAtMs = millis();
  out = c;
  Serial.printf("[carbon] %u g/kWh (%s), wind %.0f%% nuclear %.0f%% gas %.0f%%, %u forecast slots\n", c.nowGPerKwh,
                indexName(c.nowIndex), c.mix[0], c.mix[2], c.mix[6], c.count);
  return true;
}

const char *indexName(CarbonIndex i) {
  switch (i) {
    case CarbonIndex::VeryLow: return "Very low";
    case CarbonIndex::Low: return "Low";
    case CarbonIndex::Moderate: return "Moderate";
    case CarbonIndex::High: return "High";
    case CarbonIndex::VeryHigh: return "Very high";
    default: return "?";
  }
}

const char *fuelName(Fuel f) {
  static const char *const names[] = {"Wind", "Solar", "Nuclear", "Hydro", "Biomass", "Imports", "Gas", "Coal", "Other"};
  const int i = static_cast<int>(f);
  return i < static_cast<int>(Fuel::Count) ? names[i] : "?";
}

}  // namespace Carbon
