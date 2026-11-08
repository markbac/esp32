/**
 * @file    data_service.h
 * @brief   Background download task that owns all online data.
 *
 * @details A FreeRTOS task on core 0 refreshes each feed (weather, air quality,
 *          warnings, aurora, flights, radar) on its own schedule, so slow HTTPS
 *          calls never freeze the screen, buttons or web server (core 1).
 *
 *          Each fetch fills a private staging object, then copies or swaps it
 *          into the shared copy while holding a mutex. Readers must hold the
 *          lock (see @ref DataService::Guard) while using the references.
 */
#pragma once

#include "air_quality.h"
#include "aurora.h"
#include "flights.h"
#include "radar.h"
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
///@}

}  // namespace DataService
