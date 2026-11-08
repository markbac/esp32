/**
 * @file    data_service.cpp
 * @brief   Worker task, feed scheduling table and shared-data locking.
 */
#include "data_service.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <memory>

#include "config.h"

namespace {

SemaphoreHandle_t mutex = nullptr;            ///< Guards everything below.
std::atomic<bool> refreshRequested{false};    ///< Set by refreshAll().

WeatherData weatherShared;
AirQualityData airShared;
WarningList warningsShared;
AuroraData auroraShared;
FlightList flightsShared;
RadarLoop radarShared;    ///< Front buffers, read by the UI and web server.
RadarLoop radarStaging;   ///< Back buffers, written by the worker.

constexpr uint32_t WORKER_STACK = 16384;  ///< TLS handshakes need ~10 KB of stack.
constexpr uint32_t WORKER_IDLE_MS = 250;  ///< Scheduler tick.

/**
 * @brief  Fetch into a heap-allocated staging copy, then publish under the lock.
 * @tparam T      Data type (must be copy-assignable).
 * @param  shared Published copy.
 * @param  fetch  Fetcher that fills its argument on success.
 * @return true if fetched and published.
 */
template <typename T>
bool update(T &shared, bool (*fetch)(T &)) {
  std::unique_ptr<T> staging(new (std::nothrow) T());
  if (!staging || !fetch(*staging)) return false;
  DataService::lock();
  shared = *staging;
  DataService::unlock();
  return true;
}

/** @brief One scheduled data source. */
struct Feed {
  const char *name;      ///< Log label.
  bool (*run)();         ///< Fetch + publish; true on success.
  uint32_t interval;     ///< Period after success, ms.
  uint32_t lastRun;      ///< millis() of last attempt.
  bool ok;               ///< Last attempt succeeded.
  bool started;          ///< Has run at least once.
};

/** @brief All feeds, in start-up order (fast, small ones first). */
Feed feeds[] = {
    {"weather", [] { return update(weatherShared, Weather::fetch); }, WEATHER_INTERVAL_MS, 0, false, false},
    {"air", [] { return update(airShared, AirQuality::fetch); }, AIR_INTERVAL_MS, 0, false, false},
    {"warnings", [] { return update(warningsShared, Warnings::fetch); }, WARNINGS_INTERVAL_MS, 0, false, false},
    {"aurora", [] { return update(auroraShared, Aurora::fetch); }, AURORA_INTERVAL_MS, 0, false, false},
    {"flights", [] { return update(flightsShared, Flights::fetch); }, FLIGHTS_INTERVAL_MS, 0, false, false},
    {"radar",
     [] {
       // radarShared is only ever modified by this task, so reading it unlocked is safe.
       if (!Radar::fetch(radarStaging, radarShared)) return false;
       DataService::lock();
       std::swap(radarShared, radarStaging);  // Swaps buffer pointers; no pixel copy.
       DataService::unlock();
       return true;
     },
     RADAR_INTERVAL_MS, 0, false, false},
};

/** @brief Worker task body: run each feed when due (or when a refresh is requested). */
void worker(void *) {
  for (;;) {
    const bool force = refreshRequested.exchange(false);
    for (Feed &f : feeds) {
      const uint32_t period = f.ok ? f.interval : WEATHER_RETRY_MS;
      if (force || !f.started || millis() - f.lastRun >= period) {
        f.ok = f.run();
        f.lastRun = millis();
        f.started = true;
        if (!f.ok) Serial.printf("[%s] fetch failed, retry in %lus\n", f.name, WEATHER_RETRY_MS / 1000);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(WORKER_IDLE_MS));
  }
}

}  // namespace

namespace DataService {

void begin() {
  mutex = xSemaphoreCreateMutex();
  if (!Radar::allocate(radarShared) || !Radar::allocate(radarStaging)) Serial.println("[radar] buffer allocation failed");
  // Core 0 alongside the Wi-Fi stack; the UI loop and web server run on core 1.
  xTaskCreatePinnedToCore(worker, "data", WORKER_STACK, nullptr, 1, nullptr, 0);
}

void refreshAll() { refreshRequested = true; }

void lock() { xSemaphoreTake(mutex, portMAX_DELAY); }

void unlock() { xSemaphoreGive(mutex); }

const WeatherData &weather() { return weatherShared; }
const AirQualityData &air() { return airShared; }
const WarningList &warnings() { return warningsShared; }
const AuroraData &aurora() { return auroraShared; }
const RadarLoop &radar() { return radarShared; }
const FlightList &flights() { return flightsShared; }

}  // namespace DataService
