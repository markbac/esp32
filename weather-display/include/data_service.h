/**
 * @file    data_service.h
 * @brief   Background download task and the shared store for all live data.
 *
 * @details A FreeRTOS task on core 0 refreshes each online feed (weather, air
 *          quality, warnings, aurora, flights, radar) on its own schedule, so
 *          slow HTTPS calls never freeze the screen, touch or web server.
 *          Tempest UDP packets are merged into the same store by the Tempest
 *          module as they arrive.
 *
 *          Each fetch fills a private staging object, then copies or swaps it
 *          into the shared copy while holding a mutex. Readers must hold the
 *          lock (see @ref DataService::Guard) while using the references.
 */
#pragma once

#include "air_quality.h"
#include "aurora.h"
#include "carbon.h"
#include "flights.h"
#include "radar.h"
#include "tempest.h"
#include "warnings.h"
#include "weather.h"

namespace DataService {

/** @brief Allocate buffers, create the mutex and start the worker task. */
void begin();

/** @brief Ask the worker to refresh every feed as soon as possible. */
void refreshAll();

/** @brief Take the data lock (blocks). */
void lock();

/** @brief Release the data lock. */
void unlock();

/** @brief RAII helper: holds the data lock for its lifetime. */
class Guard {
 public:
  Guard() { lock(); }
  ~Guard() { unlock(); }
  Guard(const Guard &) = delete;
  Guard &operator=(const Guard &) = delete;
};

/** @name Shared data (only touch while holding the lock) */
///@{
const WeatherData &weather();
const AirQualityData &air();
const WarningList &warnings();
const AuroraData &aurora();
const RadarLoop &radar();
const FlightList &flights();
const TempestData &tempest();
const CarbonData &carbon();
///@}

/** @brief Writable Tempest store; only the Tempest module uses this, under the lock. */
TempestData &tempestMutable();

/** @brief millis() of each feed's last successful refresh, for status pages. */
struct FeedAge {
  const char *name;    ///< Feed name.
  uint32_t okAtMs;     ///< 0 = never succeeded.
  bool lastOk;         ///< Result of the most recent attempt.
};

/**
 * @brief  Copy the status of every feed.
 * @param[out] out  Array to fill.
 * @param  max      Capacity of @p out.
 * @return Number of entries written.
 */
uint8_t feedStatus(FeedAge *out, uint8_t max);

}  // namespace DataService
