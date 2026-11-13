/**
 * @file    weather.cpp
 * @brief   Open-Meteo HTTPS client and JSON parsing.
 */
#include "weather.h"
#include "log.h"

#include <ArduinoJson.h>

#include "astro.h"
#include "connectivity.h"
#include "https_get.h"

namespace {

/**
 * @brief Build the Open-Meteo request URL from the settings in config.h.
 * @return Full HTTPS URL.
 */
String buildUrl() {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" WX_LATITUDE "&longitude=" WX_LONGITUDE;
  url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"
         "weather_code,wind_speed_10m,wind_direction_10m,wind_gusts_10m,pressure_msl,precipitation";
  url += "&hourly=temperature_2m,weather_code,precipitation_probability,wind_speed_10m,is_day";
  url += "&forecast_hours=" + String(HOURLY_MAX + 1);
  url += "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,uv_index_max,sunrise,sunset,"
         "daylight_duration,sunshine_duration";
  url += "&minutely_15=precipitation&forecast_minutely_15=" + String(NOWCAST_SLOTS);
  url += "&forecast_days=" + String(DAILY_DAYS);
  url += "&wind_speed_unit=mph&temperature_unit=celsius&timezone=" WX_TIMEZONE;
  return url;
}

/**
 * @brief Day of week for a Gregorian date (Sakamoto's algorithm).
 * @return 0 = Sunday ... 6 = Saturday.
 */
uint8_t dayOfWeek(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

/**
 * @brief Copy "HH:MM" out of an ISO-8601 "YYYY-MM-DDTHH:MM" string.
 * @param iso  Source timestamp.
 * @param dst  Destination buffer of at least 6 bytes.
 */
void copyHHMM(const char *iso, char *dst) {
  if (iso && strlen(iso) >= 16) {
    memcpy(dst, iso + 11, 5);
    dst[5] = '\0';
  }
}

}  // namespace

namespace Weather {

bool fetch(WeatherData &out) {
  JsonDocument doc;
  const bool ok = Https::get(buildUrl(), "weather", [&](HTTPClient &http) {
    const DeserializationError err = deserializeJson(doc, http.getStream());
    if (err) Log::printf("[weather] JSON error: %s\n", err.c_str());
    return !err;
  });
  if (!ok) return false;

  WeatherData wx;  // Fill a temporary so a partial parse never corrupts `out`.

  // --- current -----------------------------------------------------------
  JsonObject cur = doc["current"];
  wx.current.temp = cur["temperature_2m"] | 0.0f;
  wx.current.feelsLike = cur["apparent_temperature"] | 0.0f;
  wx.current.humidity = cur["relative_humidity_2m"] | 0;
  wx.current.code = cur["weather_code"] | 0;
  wx.current.isDay = (cur["is_day"] | 1) == 1;
  wx.current.wind = cur["wind_speed_10m"] | 0.0f;
  wx.current.gust = cur["wind_gusts_10m"] | 0.0f;
  wx.current.windDir = cur["wind_direction_10m"] | 0;
  wx.current.pressure = cur["pressure_msl"] | 0.0f;
  wx.current.precip = cur["precipitation"] | 0.0f;

  // --- hourly: skip any samples before the current local hour -------------
  char nowHour[17] = "";
  tm t;
  if (Net::localTime(t)) strftime(nowHour, sizeof(nowHour), "%Y-%m-%dT%H:00", &t);

  JsonObject hourly = doc["hourly"];
  JsonArray times = hourly["time"];
  for (size_t i = 0; i < times.size() && wx.hourlyCount < HOURLY_MAX; ++i) {
    const char *ts = times[i] | "";
    if (nowHour[0] && strcmp(ts, nowHour) < 0) continue;  // ISO strings sort chronologically.
    HourlyPoint &h = wx.hourly[wx.hourlyCount++];
    h.hour = strlen(ts) >= 13 ? atoi(ts + 11) : 0;
    h.temp = hourly["temperature_2m"][i] | 0.0f;
    h.code = hourly["weather_code"][i] | 0;
    h.rainChance = hourly["precipitation_probability"][i] | 0;
    h.wind = hourly["wind_speed_10m"][i] | 0.0f;
    h.isDay = (hourly["is_day"][i] | 1) == 1;
  }

  // --- daily ---------------------------------------------------------------
  JsonObject daily = doc["daily"];
  JsonArray days = daily["time"];
  for (size_t i = 0; i < days.size() && wx.dailyCount < DAILY_DAYS; ++i) {
    int y = 0, m = 0, d = 0;
    sscanf(days[i] | "", "%d-%d-%d", &y, &m, &d);
    DailyPoint &p = wx.daily[wx.dailyCount++];
    p.weekday = (y > 0) ? dayOfWeek(y, m, d) : 0;
    p.tMax = daily["temperature_2m_max"][i] | 0.0f;
    p.tMin = daily["temperature_2m_min"][i] | 0.0f;
    p.code = daily["weather_code"][i] | 0;
    p.rainChance = daily["precipitation_probability_max"][i] | 0;
    p.uvMax = daily["uv_index_max"][i] | 0.0f;
  }
  wx.daylightSec = daily["daylight_duration"][0] | 0.0f;
  wx.sunshineSec = daily["sunshine_duration"][0] | 0.0f;

  // --- 15-minute nowcast ------------------------------------------------------
  JsonObject m15 = doc["minutely_15"];
  JsonArray slots = m15["precipitation"];
  copyHHMM(m15["time"][0] | "", wx.nowcast.firstSlot);
  for (size_t i = 0; i < slots.size() && wx.nowcast.count < NOWCAST_SLOTS; ++i) {
    wx.nowcast.mm[wx.nowcast.count++] = slots[i] | 0.0f;
  }

  copyHHMM(daily["sunrise"][0] | "", wx.sunrise);
  copyHHMM(daily["sunset"][0] | "", wx.sunset);

  wx.valid = true;
  wx.fetchedAtMs = millis();
  out = wx;

  Log::printf("[weather] %.1fC feels %.1fC, %s, hum %d%%, wind %.0fmph %s (gust %.0f); "
                "%u hourly, %u daily, %u nowcast; sun %s-%s\n",
                wx.current.temp, wx.current.feelsLike, describe(wx.current.code), wx.current.humidity,
                wx.current.wind, compass(wx.current.windDir), wx.current.gust, wx.hourlyCount,
                wx.dailyCount, wx.nowcast.count, wx.sunrise, wx.sunset);
  return true;
}

RainSoon rainSoon(const WeatherData &wx, int nowMinutes) {
  RainSoon r;
  const Nowcast &n = wx.nowcast;
  const int first = Astro::minutesOfDay(n.firstSlot);
  if (!wx.valid || n.count == 0 || first < 0 || nowMinutes < 0) return r;

  // Minutes from now until slot i starts (handles midnight wrap).
  auto until = [&](int i) {
    int d = first + i * 15 - nowMinutes;
    if (d < -720) d += 1440;
    if (d > 720) d -= 1440;
    return max(0, (d + 2) / 5 * 5);
  };

  // The slot containing "now" is the last one that has already started.
  int cur = 0;
  while (cur + 1 < n.count && until(cur + 1) == 0) ++cur;

  const bool wetNow = n.mm[cur] >= RAIN_THRESHOLD_MM;
  r.state = wetNow ? RainSoon::State::Raining : RainSoon::State::Dry;
  for (int i = cur + 1; i < n.count; ++i) {
    if ((n.mm[i] >= RAIN_THRESHOLD_MM) != wetNow) {
      r.minutes = until(i);
      if (!wetNow) r.state = RainSoon::State::Starting;
      break;
    }
  }
  return r;
}

const char *describe(int c) {
  switch (c) {
    case 0: return "Clear";
    case 1: return "Mainly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: return "Light drizzle";
    case 53: return "Drizzle";
    case 55: return "Heavy drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunder + hail";
    default: return "Unknown";
  }
}

const char *compass(int degrees) {
  static const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  degrees = ((degrees % 360) + 360) % 360;
  return dirs[((degrees + 22) % 360) / 45];
}

}  // namespace Weather
