/**
 * @file    tempest.cpp
 * @brief   Tempest UDP parsing, de-duplication and derived values.
 *
 * @details Packets are received by AsyncUDP (its own task); each one is parsed
 *          with ArduinoJson and merged into the shared TempestData while
 *          holding the DataService lock. Field order of obs_st follows the
 *          WeatherFlow UDP API v171.
 */
#include "tempest.h"

#include <ArduinoJson.h>
#include <AsyncUDP.h>
#include <time.h>

#include "config.h"
#include "connectivity.h"
#include "data_service.h"

namespace {

AsyncUDP udp;                    ///< Listener on TEMPEST_PORT.
uint32_t lastObsTime = 0;        ///< De-duplication keys (epoch of last processed message).
uint32_t lastRapidTime = 0;
uint32_t lastStrikeEvt = 0;
uint32_t lastPrecipEvt = 0;
int lastDay = -1;                ///< Local day-of-year, for the midnight reset.

float rainMinute[60] = {};       ///< Per-minute rain ring for the 60-minute total.
uint16_t strikeMinute[60] = {};  ///< Per-minute strike ring.
uint8_t ringPos = 0;

constexpr float MS_TO_MPH = 2.236936f;

/** @brief Sea-level pressure from station pressure (hypsometric approximation). */
float seaLevel(float stationHpa, float tempC) {
  const float h = TEMPEST_ELEVATION_M;
  return stationHpa * powf(1.0f - 0.0065f * h / (tempC + 0.0065f * h + 273.15f), -5.257f);
}

/** @brief Magnus-formula dew point. */
float dewPoint(float t, float rh) {
  if (rh <= 0) return NAN;
  const float a = 17.625f, b = 243.04f;
  const float g = logf(rh / 100.0f) + a * t / (b + t);
  return b * g / (a - g);
}

/** @brief Wind chill below 10 C, heat index above 26.7 C, otherwise air temperature. */
float feelsLike(float t, float rh, float windMph) {
  const float kmh = windMph * 1.609344f;
  if (t <= 10.0f && kmh > 4.8f) {
    const float v = powf(kmh, 0.16f);
    return 13.12f + 0.6215f * t - 11.37f * v + 0.3965f * t * v;
  }
  if (t >= 26.7f) {
    const float f = t * 9 / 5 + 32;
    const float hi = -42.379f + 2.04901523f * f + 10.14333127f * rh - 0.22475541f * f * rh - 6.83783e-3f * f * f -
                     5.481717e-2f * rh * rh + 1.22874e-3f * f * f * rh + 8.5282e-4f * f * rh * rh -
                     1.99e-6f * f * f * rh * rh;
    return (hi - 32) * 5 / 9;
  }
  return t;
}

/** @brief Append a value to a fixed trend array, dropping the oldest when full. */
void pushTrend(float *arr, uint16_t count, float v) {
  if (count < TEMPEST_TREND_MIN) {
    arr[count] = v;
  } else {
    memmove(arr, arr + 1, (TEMPEST_TREND_MIN - 1) * sizeof(float));
    arr[TEMPEST_TREND_MIN - 1] = v;
  }
}

/** @brief Reset daily totals when the local date changes. */
void checkMidnight(TempestData &d) {
  tm lt;
  if (!Net::localTime(lt)) return;
  if (lastDay >= 0 && lt.tm_yday != lastDay) {
    d.rainTodayMm = 0;
    d.strikesToday = 0;
    d.maxRateTodayMmH = 0;
  }
  lastDay = lt.tm_yday;
}

/** @brief Handle an obs_st message. Caller holds the lock. */
void onObservation(TempestData &d, JsonArrayConst o) {
  const uint32_t t = o[0] | 0;
  if (t == 0 || t == lastObsTime) return;  // Duplicate broadcast.
  lastObsTime = t;
  checkMidnight(d);

  TempestObs &ob = d.obs;
  ob.time = t;
  ob.windLull = (o[1] | 0.0f) * MS_TO_MPH;
  ob.windAvg = (o[2] | 0.0f) * MS_TO_MPH;
  ob.windGust = (o[3] | 0.0f) * MS_TO_MPH;
  ob.windDir = o[4] | 0;
  ob.pressureStation = o[6] | 0.0f;
  ob.tempC = o[7] | 0.0f;
  ob.humidity = o[8] | 0.0f;
  ob.lux = o[9] | 0.0f;
  ob.uv = o[10] | 0.0f;
  ob.solar = o[11] | 0.0f;
  ob.rainMm = o[12] | 0.0f;
  ob.precipType = o[13] | 0;
  ob.strikeKm = o[14] | 0.0f;
  ob.strikes = o[15] | 0;
  ob.batteryV = o[16] | 0.0f;
  ob.pressureSea = seaLevel(ob.pressureStation, ob.tempC);

  d.feelsC = feelsLike(ob.tempC, ob.humidity, ob.windAvg);
  d.dewPointC = dewPoint(ob.tempC, ob.humidity);

  // Rolling 60-minute and daily totals.
  ringPos = (ringPos + 1) % 60;
  rainMinute[ringPos] = ob.rainMm;
  strikeMinute[ringPos] = ob.strikes;
  d.rainHourMm = 0;
  d.strikesHour = 0;
  for (int i = 0; i < 60; ++i) {
    d.rainHourMm += rainMinute[i];
    d.strikesHour += strikeMinute[i];
  }
  d.rainTodayMm += ob.rainMm;
  d.rainRateMmH = ob.rainMm * 60.0f;  // 1-minute accumulation -> per hour.
  d.maxRateTodayMmH = max(d.maxRateTodayMmH, d.rainRateMmH);
  d.windowRateMmH = max(d.windowRateMmH, d.rainRateMmH);
  d.strikesToday += ob.strikes;

  d.windowRainMm += ob.rainMm;
  d.windowGustMph = max(d.windowGustMph, ob.windGust);
  d.windowStrikes += ob.strikes;

  pushTrend(d.trendTemp, d.trendCount, ob.tempC);
  pushTrend(d.trendPress, d.trendCount, ob.pressureSea);
  pushTrend(d.trendWind, d.trendCount, ob.windAvg);
  if (d.trendCount < TEMPEST_TREND_MIN) ++d.trendCount;

  d.valid = true;
  d.obsAtMs = millis();
}

/** @brief Route one decoded message to its handler. Caller holds the lock. */
void dispatch(TempestData &d, JsonDocument &doc, const IPAddress &from) {
  const char *type = doc["type"] | "";
  ++d.packets;
  strlcpy(d.hubIp, from.toString().c_str(), sizeof(d.hubIp));

  if (strcmp(type, "rapid_wind") == 0) {
    JsonArrayConst ob = doc["ob"];
    const uint32_t t = ob[0] | 0;
    if (t == lastRapidTime) return;
    lastRapidTime = t;
    d.rapidMph = (ob[1] | 0.0f) * MS_TO_MPH;
    d.rapidDir = ob[2] | 0;
    d.rapidValid = true;
    d.rapidAtMs = millis();
  } else if (strcmp(type, "obs_st") == 0) {
    strlcpy(d.sensorSerial, doc["serial_number"] | "", sizeof(d.sensorSerial));
    strlcpy(d.hubSerial, doc["hub_sn"] | "", sizeof(d.hubSerial));
    d.sensorFirmware = doc["firmware_revision"] | 0;
    onObservation(d, doc["obs"][0]);
  } else if (strcmp(type, "evt_strike") == 0) {
    JsonArrayConst e = doc["evt"];
    const uint32_t t = e[0] | 0;
    if (t == lastStrikeEvt) return;
    lastStrikeEvt = t;
    d.lastStrikeTime = t;
    d.lastStrikeKm = e[1] | 0.0f;
    Serial.printf("[tempest] lightning strike %.0f km away\n", d.lastStrikeKm);
  } else if (strcmp(type, "evt_precip") == 0) {
    const uint32_t t = doc["evt"][0] | 0;
    if (t == lastPrecipEvt) return;
    lastPrecipEvt = t;
    d.rainStartTime = t;
    Serial.println("[tempest] rain started");
  } else if (strcmp(type, "device_status") == 0) {
    d.sensorVoltage = doc["voltage"] | 0.0f;
    d.sensorRssi = doc["rssi"] | 0;
    d.hubRssi = doc["hub_rssi"] | 0;
    d.sensorUptime = doc["uptime"] | 0;
    d.sensorStatus = doc["sensor_status"] | 0;
  } else if (strcmp(type, "hub_status") == 0) {
    d.hubWifiRssi = doc["rssi"] | 0;
    d.hubUptime = doc["uptime"] | 0;
    d.hubFirmware = atoi(doc["firmware_revision"] | "0");
  }
}

/** @brief AsyncUDP callback (runs on the AsyncUDP task). */
void onPacket(AsyncUDPPacket &packet) {
  JsonDocument doc;
  if (deserializeJson(doc, packet.data(), packet.length())) return;
  const IPAddress from = packet.remoteIP();
  DataService::Guard g;
  dispatch(DataService::tempestMutable(), doc, from);
}

}  // namespace

bool TempestData::fresh() const { return valid && millis() - obsAtMs < TEMPEST_STALE_MS; }

float TempestData::pressureTrend(uint16_t &minutes) const {
  minutes = trendCount > 0 ? trendCount - 1 : 0;
  if (minutes < 30) return NAN;
  return trendPress[trendCount - 1] - trendPress[0];
}

namespace Tempest {

bool begin() {
  if (!udp.listen(TEMPEST_PORT)) {
    Serial.println("[tempest] UDP listen failed");
    return false;
  }
  udp.onPacket(onPacket);
  Serial.printf("[tempest] listening on UDP %u\n", TEMPEST_PORT);
  return true;
}

void seedRainToday(float mm) {
  DataService::Guard g;
  DataService::tempestMutable().rainTodayMm = mm;
  tm lt;
  if (Net::localTime(lt)) lastDay = lt.tm_yday;
}

void takeWindow(float &rainMm, float &gustMph, uint16_t &strikes, float &maxRateMmH) {
  DataService::Guard g;
  TempestData &d = DataService::tempestMutable();
  rainMm = d.windowRainMm;
  gustMph = d.windowGustMph;
  strikes = d.windowStrikes;
  maxRateMmH = d.windowRateMmH;
  d.windowRainMm = 0;
  d.windowGustMph = 0;
  d.windowStrikes = 0;
  d.windowRateMmH = 0;
}

const char *rainIntensity(float r) {
  if (r < 0.05f) return "None";
  if (r < 2.5f) return "Light";
  if (r < 10.0f) return "Moderate";
  if (r < 50.0f) return "Heavy";
  return "Violent";
}

const char *precipName(uint8_t type) {
  switch (type) {
    case 1: return "Rain";
    case 2: return "Hail";
    case 3: return "Rain + hail";
    default: return "None";
  }
}

}  // namespace Tempest
