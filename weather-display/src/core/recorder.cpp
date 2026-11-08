/**
 * @file    recorder.cpp
 * @brief   Builds HistoryRecords from DataService snapshots.
 */
#include "recorder.h"

#include "config.h"
#include "connectivity.h"
#include "data_service.h"
#include "history.h"

namespace {

uint32_t lastSlot = 0;  ///< Quarter-hour slot (epoch / interval) last logged.

/** @brief Scale, clamp and round to an integer field. */
template <typename T>
T scaled(float v, float scale, float lo, float hi) {
  return static_cast<T>(lroundf(constrain(v * scale, lo, hi)));
}

/** @brief Snapshot current data into a record. */
HistoryRecord build(time_t now, const Recorder::DeviceBattery &battery) {
  HistoryRecord r{};
  r.time = static_cast<uint32_t>(now);

  float tRain = 0, tGust = 0, tRate = 0;
  uint16_t tStrikes = 0;
  Tempest::takeWindow(tRain, tGust, tStrikes, tRate);

  DataService::Guard g;
  const WeatherData &wx = DataService::weather();
  if (wx.valid) {
    const CurrentWeather &c = wx.current;
    r.flags |= HF_WEATHER;
    r.temp10 = scaled<int16_t>(c.temp, 10, -32000, 32000);
    r.feels10 = scaled<int16_t>(c.feelsLike, 10, -32000, 32000);
    r.humidity = static_cast<uint8_t>(constrain(c.humidity, 0, 100));
    r.pressure10 = scaled<uint16_t>(c.pressure, 10, 0, 65535);
    r.rain100 = scaled<uint16_t>(c.precip, 100, 0, 65535);
    r.wind10 = scaled<uint16_t>(c.wind, 10, 0, 65535);
    r.gust10 = scaled<uint16_t>(c.gust, 10, 0, 65535);
    r.windDir = static_cast<uint16_t>(c.windDir);
    r.code = static_cast<uint8_t>(c.code);
  }
  const AirQualityData &aq = DataService::air();
  if (aq.valid) {
    r.flags |= HF_AIR;
    r.eaqi = static_cast<uint8_t>(constrain(aq.eaqi, 0, 255));
    r.pm25x10 = scaled<uint16_t>(aq.pm25, 10, 0, 65535);
  }
  const AuroraData &au = DataService::aurora();
  if (au.valid) {
    r.flags |= HF_AURORA;
    r.aurora10 = scaled<uint16_t>(Aurora::latestActivity(au), 10, 0, 65535);
    r.auroraLevel = static_cast<uint8_t>(au.level);
  }
  const TempestData &t = DataService::tempest();
  if (t.fresh()) {
    r.flags |= HF_TEMPEST;
    r.tTemp10 = scaled<int16_t>(t.obs.tempC, 10, -32000, 32000);
    r.tHumidity = static_cast<uint8_t>(constrain(lroundf(t.obs.humidity), 0, 100));
    r.tPressure10 = scaled<uint16_t>(t.obs.pressureSea, 10, 0, 65535);
    r.tWind10 = scaled<uint16_t>(t.obs.windAvg, 10, 0, 65535);
    r.tGust10 = scaled<uint16_t>(max(tGust, t.obs.windGust), 10, 0, 65535);
    r.tWindDir = t.obs.windDir;
    r.tRain100 = scaled<uint16_t>(tRain, 100, 0, 65535);
    r.tUv10 = scaled<uint8_t>(t.obs.uv, 10, 0, 255);
    r.tSolar = scaled<uint16_t>(t.obs.solar, 1, 0, 65535);
    r.tStrikes = static_cast<uint8_t>(min<uint16_t>(tStrikes, 255));
    r.tBattMv = scaled<uint16_t>(t.obs.batteryV, 1000, 0, 65535);
    r.tRate10 = scaled<uint16_t>(tRate, 10, 0, 65535);
  }
  const CarbonData &ci = DataService::carbon();
  if (ci.valid) {
    r.flags |= HF_CARBON;
    r.carbon = ci.nowGPerKwh;
  }
  r.aircraft = static_cast<uint8_t>(min<uint16_t>(DataService::flights().total, 255));

  if (battery.present) {
    r.flags |= HF_BATTERY;
    r.battPct = static_cast<uint8_t>(lroundf(battery.percent));
    r.battMv = scaled<uint16_t>(battery.voltage, 1000, 0, 65535);
  } else {
    r.battPct = 255;
  }
  r.rssi = static_cast<int8_t>(Net::rssi());
  return r;
}

}  // namespace

namespace Recorder {

void begin() {
  lastSlot = History::lastTime() / HISTORY_INTERVAL_S;  // Don't double-log after a reboot.

  // Seed Tempest's rain-today from records logged since local midnight.
  tm lt;
  if (!Net::localTime(lt)) return;
  lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
  const time_t midnight = mktime(&lt);
  std::vector<HistoryRecord> recs;
  History::read(static_cast<uint32_t>(midnight), recs);
  float mm = 0;
  for (const HistoryRecord &r : recs) {
    if (r.flags & HF_TEMPEST) mm += r.tRain100 / 100.0f;
  }
  Tempest::seedRainToday(mm);
  Serial.printf("[history] rain today restored: %.1f mm from %u records\n", mm, recs.size());
}

bool poll(const DeviceBattery &battery) {
  if (!Net::timeValid()) return false;
  const time_t now = time(nullptr);
  const uint32_t slot = now / HISTORY_INTERVAL_S;
  if (slot == lastSlot) return false;
  {
    DataService::Guard g;
    if (!DataService::weather().valid && !DataService::tempest().fresh()) return false;  // Nothing to log yet.
  }
  lastSlot = slot;
  const HistoryRecord r = build(now, battery);
  const bool ok = History::append(r);
  Serial.printf("[history] %s record %u (%.1fC, Tempest %s)\n", ok ? "logged" : "FAILED to log", History::count(),
                (r.flags & HF_TEMPEST ? r.tTemp10 : r.temp10) / 10.0f, r.flags & HF_TEMPEST ? "yes" : "no");
  return ok;
}

}  // namespace Recorder
