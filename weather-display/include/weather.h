/**
 * @file    weather.h
 * @brief   Weather data model and Open-Meteo client.
 *
 * @details One HTTPS request returns current conditions, an hourly series and
 *          a daily series. Units: temperature in degrees C, wind in mph.
 *          Weather conditions use WMO codes (see @ref Weather::describe).
 */
#pragma once

#include <Arduino.h>

#include "config.h"

/** @brief Conditions right now. */
struct CurrentWeather {
  float temp = 0;        ///< Air temperature, deg C.
  float feelsLike = 0;   ///< Apparent temperature, deg C.
  int humidity = 0;      ///< Relative humidity, %.
  int code = 0;          ///< WMO weather code.
  bool isDay = true;     ///< Daylight flag (selects sun vs moon icons).
  float wind = 0;        ///< Mean wind speed, mph.
  float gust = 0;        ///< Gust speed, mph.
  int windDir = 0;       ///< Direction wind blows from, degrees.
  float pressure = 0;    ///< Mean sea-level pressure, hPa.
  float precip = 0;      ///< Precipitation in the preceding 15 minutes, mm.
};

/** @brief One hourly forecast sample. */
struct HourlyPoint {
  uint8_t hour = 0;      ///< Local hour of day, 0-23.
  float temp = 0;        ///< deg C.
  int code = 0;          ///< WMO weather code.
  int rainChance = 0;    ///< Precipitation probability, %.
  float wind = 0;        ///< mph.
  bool isDay = true;     ///< Daylight flag.
};

/** @brief One daily forecast summary. */
struct DailyPoint {
  uint8_t weekday = 0;   ///< 0 = Sunday ... 6 = Saturday.
  float tMax = 0;        ///< Daily high, deg C.
  float tMin = 0;        ///< Daily low, deg C.
  int code = 0;          ///< Dominant WMO weather code.
  int rainChance = 0;    ///< Max precipitation probability, %.
  float uvMax = 0;       ///< Peak UV index.
};

/** @brief Number of 15-minute precipitation slots fetched (2 hours). */
constexpr uint8_t NOWCAST_SLOTS = 8;

/** @brief Short-range precipitation forecast in 15-minute steps. */
struct Nowcast {
  uint8_t count = 0;              ///< Valid slots.
  char firstSlot[6] = "--:--";    ///< Local start time of slot 0, "HH:MM".
  float mm[NOWCAST_SLOTS] = {};   ///< Precipitation per slot, mm.
};

/** @brief Interpretation of the nowcast for "rain soon" messages. */
struct RainSoon {
  enum class State : uint8_t {
    Unknown,   ///< No nowcast or clock yet.
    Dry,       ///< No rain in the next 2 hours.
    Starting,  ///< Dry now; rain expected in @ref minutes.
    Raining,   ///< Raining now; stops in @ref minutes (-1 = not within 2 h).
  };
  State state = State::Unknown;  ///< Overall outlook.
  int minutes = -1;              ///< Minutes until the change, rounded to 5.
};

/** @brief Precipitation (mm per 15 min) counted as "rain". */
constexpr float RAIN_THRESHOLD_MM = 0.1f;

/** @brief Complete result of one successful fetch. */
struct WeatherData {
  bool valid = false;               ///< False until the first successful fetch.
  uint32_t fetchedAtMs = 0;         ///< millis() at fetch time.
  CurrentWeather current;           ///< Conditions now.
  HourlyPoint hourly[HOURLY_MAX];   ///< Hourly samples, starting at the current hour.
  uint8_t hourlyCount = 0;          ///< Valid entries in @ref hourly.
  DailyPoint daily[DAILY_DAYS];     ///< Daily summaries, starting today.
  uint8_t dailyCount = 0;           ///< Valid entries in @ref daily.
  char sunrise[6] = "--:--";        ///< Today's sunrise, "HH:MM" local.
  char sunset[6] = "--:--";         ///< Today's sunset, "HH:MM" local.
  float daylightSec = 0;            ///< Today's daylight duration, seconds.
  float sunshineSec = 0;            ///< Today's forecast sunshine, seconds.
  Nowcast nowcast;                  ///< Next 2 hours in 15-minute slots.
};

namespace Weather {

/**
 * @brief  Download and parse the forecast.
 * @param[out] out  Overwritten only when the fetch fully succeeds.
 * @return true on success; on failure @p out is left untouched.
 */
bool fetch(WeatherData &out);

/**
 * @brief  Work out whether rain is starting or stopping soon.
 * @param  wx          Forecast containing a nowcast.
 * @param  nowMinutes  Current local time, minutes after midnight.
 * @return Outlook; State::Unknown if there is no nowcast.
 */
RainSoon rainSoon(const WeatherData &wx, int nowMinutes);

/** @brief Short human-readable text for a WMO weather code. */
const char *describe(int wmoCode);

/** @brief 8-point compass label ("N", "NE", ...) for a bearing in degrees. */
const char *compass(int degrees);

}  // namespace Weather
