/**
 * @file    tempest.h
 * @brief   WeatherFlow Tempest station: local UDP broadcast listener.
 *
 * @details The Tempest hub broadcasts JSON on UDP port 50222 to the whole LAN
 *          (no cloud or API key needed). Messages used:
 *          | type          | rate   | content                                         |
 *          |---------------|--------|-------------------------------------------------|
 *          | rapid_wind    | 3 s    | instantaneous wind speed (m/s) and direction    |
 *          | obs_st        | 1 min  | full observation (see @ref TempestObs)          |
 *          | evt_precip    | event  | rain has started                                |
 *          | evt_strike    | event  | lightning strike: distance (km), energy         |
 *          | device_status | 1 min  | sensor battery, RSSI, uptime, sensor faults     |
 *          | hub_status    | 10 s   | hub firmware, Wi-Fi RSSI, uptime                |
 *
 *          Every packet is seen twice on this network, so messages are
 *          de-duplicated by timestamp. Parsed data is published into the
 *          DataService shared store under its lock (see DataService::tempest()).
 *
 *          Units stored: deg C, %, hPa, mph, mm, W/m2, lux, km.
 */
#pragma once

#include <Arduino.h>

/** @brief Minutes of 1-minute samples kept for trends and sparklines. */
constexpr uint16_t TEMPEST_TREND_MIN = 180;

/** @brief One obs_st observation, converted to display units. */
struct TempestObs {
  uint32_t time = 0;         ///< UTC epoch of the observation.
  float tempC = 0;           ///< Air temperature.
  float humidity = 0;        ///< Relative humidity, %.
  float pressureStation = 0; ///< Station pressure, hPa.
  float pressureSea = 0;     ///< Reduced to sea level, hPa.
  float windLull = 0;        ///< Minimum wind over the interval, mph.
  float windAvg = 0;         ///< Average wind, mph.
  float windGust = 0;        ///< Maximum gust, mph.
  uint16_t windDir = 0;      ///< Average direction, degrees.
  float lux = 0;             ///< Illuminance.
  float uv = 0;              ///< UV index.
  float solar = 0;           ///< Solar radiation, W/m2.
  float rainMm = 0;          ///< Rain during this 1-minute interval, mm.
  uint8_t precipType = 0;    ///< 0 none, 1 rain, 2 hail, 3 rain + hail.
  float strikeKm = 0;        ///< Average lightning distance this interval.
  uint16_t strikes = 0;      ///< Lightning strikes this interval.
  float batteryV = 0;        ///< Sensor battery voltage.
};

/** @brief Everything known about the Tempest station. */
struct TempestData {
  bool valid = false;            ///< At least one obs_st received.
  uint32_t obsAtMs = 0;          ///< millis() when the last obs_st arrived.
  TempestObs obs;                ///< Latest observation.

  float feelsC = 0;              ///< Wind chill / heat index / air temp.
  float dewPointC = 0;           ///< Dew point.

  bool rapidValid = false;       ///< rapid_wind received.
  uint32_t rapidAtMs = 0;        ///< millis() of last rapid_wind.
  float rapidMph = 0;            ///< Instantaneous wind speed.
  uint16_t rapidDir = 0;         ///< Instantaneous wind direction.

  float rainTodayMm = 0;         ///< Since local midnight.
  float rainHourMm = 0;          ///< Last 60 minutes.
  uint32_t rainStartTime = 0;    ///< Epoch of the last evt_precip (0 = none).
  uint16_t strikesToday = 0;     ///< Lightning strikes since local midnight.
  uint16_t strikesHour = 0;      ///< Strikes in the last 60 minutes.
  float lastStrikeKm = 0;        ///< Distance of the most recent strike.
  uint32_t lastStrikeTime = 0;   ///< Epoch of the most recent strike (0 = none).

  float windowRainMm = 0;        ///< Rain since the last history record.
  float windowGustMph = 0;       ///< Highest gust since the last history record.
  uint16_t windowStrikes = 0;    ///< Strikes since the last history record.

  uint16_t trendCount = 0;                   ///< Valid 1-minute samples (oldest first).
  float trendTemp[TEMPEST_TREND_MIN] = {};   ///< Air temperature history.
  float trendPress[TEMPEST_TREND_MIN] = {};  ///< Sea-level pressure history.
  float trendWind[TEMPEST_TREND_MIN] = {};   ///< Average wind history.

  char sensorSerial[16] = "";    ///< e.g. "ST-00209043".
  char hubSerial[16] = "";       ///< e.g. "HB-00214435".
  char hubIp[16] = "";           ///< Address packets come from.
  float sensorVoltage = 0;       ///< From device_status.
  int sensorRssi = 0;            ///< Sensor's view of the hub radio, dBm.
  int hubRssi = 0;               ///< Hub's view of the sensor radio, dBm.
  int hubWifiRssi = 0;           ///< Hub Wi-Fi signal, dBm.
  uint32_t sensorUptime = 0;     ///< Seconds.
  uint32_t hubUptime = 0;        ///< Seconds.
  uint16_t sensorFirmware = 0;   ///< Sensor firmware revision.
  uint16_t hubFirmware = 0;      ///< Hub firmware revision.
  uint32_t sensorStatus = 0;     ///< Fault bit field (0 = OK).
  uint32_t packets = 0;          ///< Unique packets processed.

  /** @brief True if an observation arrived within TEMPEST_STALE_MS. */
  bool fresh() const;

  /**
   * @brief Pressure change over the trend window.
   * @param[out] minutes  Span the change covers.
   * @return hPa change (NAN if under 30 minutes of data).
   */
  float pressureTrend(uint16_t &minutes) const;
};

namespace Tempest {

/** @brief Start listening for broadcasts (call after Wi-Fi is up). */
bool begin();

/** @brief Seed today's rain total (e.g. from the history log after a reboot). */
void seedRainToday(float mm);

/**
 * @brief  Read and reset the accumulators used for one history record.
 * @param[out] rainMm   Rain since the last call.
 * @param[out] gustMph  Highest gust since the last call.
 * @param[out] strikes  Strikes since the last call.
 */
void takeWindow(float &rainMm, float &gustMph, uint16_t &strikes);

/** @brief Name for an obs_st precipitation type. */
const char *precipName(uint8_t type);

}  // namespace Tempest
