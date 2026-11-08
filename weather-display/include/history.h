/**
 * @file    history.h
 * @brief   Persistent quarter-hourly log of weather, air, device and sky data.
 *
 * @details Records are fixed-size and stored in a ring file on LittleFS (the
 *          128 KB "spiffs" partition), so the log survives reboots and power
 *          loss and never grows: when full, the oldest record is overwritten.
 *          Capacity is HISTORY_DAYS of 15-minute records: 8 days on the
 *          Feather (128 KB partition), 90 days on the CrowPanel (~500 KB).
 *
 *          File layout: a 16-byte @ref HistoryHeader followed by
 *          HISTORY_CAPACITY slots of @ref HistoryRecord.
 *
 *          Thread-safe: append() runs on the UI loop, read() on the web task.
 */
#pragma once

#include <Arduino.h>

#include <vector>

/** @brief One logged sample (packed, scaled integers; layout version 3). */
struct __attribute__((packed)) HistoryRecord {
  uint32_t time;        ///< UTC epoch seconds.
  int16_t temp10;       ///< Air temperature, 0.1 deg C.
  int16_t feels10;      ///< Apparent temperature, 0.1 deg C.
  uint8_t humidity;     ///< Relative humidity, %.
  uint16_t pressure10;  ///< Mean sea-level pressure, 0.1 hPa.
  uint16_t rain100;     ///< Precipitation in the preceding 15 min, 0.01 mm.
  uint16_t wind10;      ///< Mean wind, 0.1 mph.
  uint16_t gust10;      ///< Gusts, 0.1 mph.
  uint16_t windDir;     ///< Wind direction, degrees.
  uint8_t code;         ///< WMO weather code.
  uint8_t eaqi;         ///< European AQI (capped at 255).
  uint16_t pm25x10;     ///< PM2.5, 0.1 ug/m3.
  uint8_t battPct;      ///< Battery %, 255 = no battery.
  uint16_t battMv;      ///< Battery voltage, mV.
  int8_t rssi;          ///< Wi-Fi RSSI, dBm (0 = offline).
  uint8_t aircraft;     ///< Aircraft within range (capped at 255).
  uint16_t aurora10;    ///< Latest AuroraWatch activity, 0.1 nT.
  uint8_t auroraLevel;  ///< AuroraLevel enum value.
  uint8_t flags;        ///< Bit 0 weather, 1 air, 2 battery, 3 aurora, 4 Tempest valid.
  // --- Tempest station (valid when HF_TEMPEST) ---
  int16_t tTemp10;      ///< Air temperature, 0.1 deg C.
  uint8_t tHumidity;    ///< Relative humidity, %.
  uint16_t tPressure10; ///< Sea-level pressure, 0.1 hPa.
  uint16_t tWind10;     ///< Average wind at log time, 0.1 mph.
  uint16_t tGust10;     ///< Highest gust in the interval, 0.1 mph.
  uint16_t tWindDir;    ///< Wind direction, degrees.
  uint16_t tRain100;    ///< Rain in the interval, 0.01 mm.
  uint8_t tUv10;        ///< UV index, 0.1 units.
  uint16_t tSolar;      ///< Solar radiation, W/m2.
  uint8_t tStrikes;     ///< Lightning strikes in the interval.
  uint16_t tBattMv;     ///< Sensor battery, mV.
  uint16_t tRate10;     ///< Highest rain rate in the interval, 0.1 mm/h.
  uint16_t carbon;      ///< Grid carbon intensity, gCO2/kWh (valid when HF_CARBON).
};

/** @name HistoryRecord::flags bits */
///@{
constexpr uint8_t HF_WEATHER = 1 << 0;
constexpr uint8_t HF_AIR = 1 << 1;
constexpr uint8_t HF_BATTERY = 1 << 2;
constexpr uint8_t HF_AURORA = 1 << 3;
constexpr uint8_t HF_TEMPEST = 1 << 4;
constexpr uint8_t HF_CARBON = 1 << 5;
///@}

namespace History {

/**
 * @brief  Mount LittleFS (formatting it on first use) and open or create the log.
 * @return true if the log is usable.
 */
bool begin();

/** @brief Append a record, overwriting the oldest when full. */
bool append(const HistoryRecord &r);

/** @brief Number of records stored. */
uint16_t count();

/** @brief Time of the newest record (0 if empty). */
uint32_t lastTime();

/**
 * @brief  Copy records newer than @p since, oldest first.
 * @param  since  UTC epoch; 0 = everything.
 * @param[out] out  Replaced with the matching records.
 * @return true on success.
 */
bool read(uint32_t since, std::vector<HistoryRecord> &out);

}  // namespace History
