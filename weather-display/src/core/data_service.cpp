/**
 * @file    data_service.cpp
 * @brief   Worker task, feed scheduling table, radar ring and shared-data locking.
 */
#include "data_service.h"
#include "log.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <memory>

#include "config.h"

namespace {

SemaphoreHandle_t mutex = nullptr;            ///< Guards everything below.
std::atomic<bool> refreshRequested{false};    ///< Set by refreshAll().
std::atomic<uint32_t> feedRequested{0};       ///< Bit per feed index, set by refresh().

WeatherData weatherShared;
AirQualityData airShared;
WarningList warningsShared;
AuroraData auroraShared;
FlightList flightsShared;
TempestData tempestShared;
CarbonData carbonShared;
HomelabData homelabShared;
HaData haShared;
RadarLoop radarShared;              ///< Frames shown by the UI and web server.
uint16_t *radarSpare = nullptr;     ///< Buffer the worker composes the next frame into.

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

/**
 * @brief Insert a freshly composed frame (in radarSpare) at the newest end of the ring.
 *        Pointer moves only; caller holds the lock.
 */
void insertFrame(time_t t) {
  RadarLoop &r = radarShared;
  if (r.count < RADAR_FRAMES) {
    std::swap(r.frames[r.count].pixels, radarSpare);
    r.frames[r.count].time = t;
    ++r.count;
  } else {
    uint16_t *oldest = r.frames[0].pixels;
    for (int i = 0; i < RADAR_FRAMES - 1; ++i) r.frames[i] = r.frames[i + 1];
    r.frames[RADAR_FRAMES - 1] = {t, radarSpare};
    radarSpare = oldest;  // Recycled for the next frame.
  }
  r.valid = true;
}

/** @brief Radar feed: download only frames newer than the newest one held. */
bool updateRadar() {
  if (!radarSpare) return false;
  RadarFrameRef refs[RADAR_FRAMES];
  uint8_t n = 0;
  if (!Radar::listFrames(refs, n)) return false;

  // Only this task modifies radarShared, so reading it unlocked is safe.
  const time_t newest = radarShared.count ? radarShared.frames[radarShared.count - 1].time : 0;
  uint8_t added = 0;
  for (uint8_t i = 0; i < n; ++i) {
    if (refs[i].time <= newest) continue;
    if (!Radar::compose(refs[i], radarSpare)) {
      Log::printf("[radar] frame %ld failed\n", static_cast<long>(refs[i].time));
      continue;
    }
    DataService::lock();
    insertFrame(refs[i].time);
    radarShared.fetchedAtMs = millis();
    DataService::unlock();
    ++added;
    Flights::resolvePending();  // Keep tap-driven route/photo lookups responsive during a long first load.
  }
  const RadarEta eta = Radar::estimate(radarShared);  // Reads frames only; this task owns them.
  DataService::lock();
  radarShared.eta = eta;
  DataService::unlock();
  Log::printf("[radar] %u new frame(s), %u in loop; eta %s here=%d approaching=%d %d min %.0f km/h from %d, cover %.0f%%\n",
                added, radarShared.count, eta.valid ? "ok" : "n/a", eta.rainingHere, eta.approaching, eta.minutes,
                eta.speedKmh, eta.fromDeg, eta.coverage * 100);
  return radarShared.count > 0;
}

/** @brief One scheduled data source. */
struct Feed {
  const char *name;      ///< Log label.
  bool (*run)();         ///< Fetch + publish; true on success.
  uint32_t interval;     ///< Period after success, ms.
  uint32_t lastRun;      ///< millis() of last attempt.
  uint32_t okAt;         ///< millis() of last success.
  bool ok;               ///< Last attempt succeeded.
  bool started;          ///< Has run at least once.
  bool quiet;            ///< Don't log failures (high-frequency internal feeds).
};

/** @brief All feeds, in start-up order (fast, small ones first). */
Feed feeds[] = {
    {"weather", [] { return update(weatherShared, Weather::fetch); }, WEATHER_INTERVAL_MS},
    {"air", [] { return update(airShared, AirQuality::fetch); }, AIR_INTERVAL_MS},
    {"warnings", [] { return update(warningsShared, Warnings::fetch); }, WARNINGS_INTERVAL_MS},
    {"aurora", [] { return update(auroraShared, Aurora::fetch); }, AURORA_INTERVAL_MS},
    {"carbon", [] { return update(carbonShared, Carbon::fetch); }, CARBON_INTERVAL_MS},
    {"flights", [] { return update(flightsShared, Flights::fetch); }, FLIGHTS_INTERVAL_MS},
    {"routes", [] { return Flights::resolvePending(); }, 500, 0, 0, false, false, true},
#if ENABLE_HOMELAB
    {"homelab", [] { return update(homelabShared, Homelab::fetch); }, HOMELAB_INTERVAL_MS},
    {"homeassistant",
     [] {
       // Keeps the last good data on failure but records the HTTP status (e.g. 401 = bad token).
       if (!HomeAssistant::configured()) return true;
       HaData staging;
       const bool ok = HomeAssistant::fetch(staging);
       DataService::lock();
       if (ok) haShared = staging;
       else haShared.lastStatus = staging.lastStatus;
       haShared.configured = true;
       DataService::unlock();
       return ok;
     },
     HA_INTERVAL_MS},
#endif
    {"radar", updateRadar, RADAR_INTERVAL_MS},
};

/** @brief Worker task body: run each feed when due (or when a refresh is requested). */
void worker(void *) {
  for (;;) {
    const bool force = refreshRequested.exchange(false);
    const uint32_t one = feedRequested.exchange(0);
    for (size_t i = 0; i < sizeof(feeds) / sizeof(feeds[0]); ++i) {
      Feed &f = feeds[i];
      const uint32_t period = f.ok ? f.interval : min(f.interval, WEATHER_RETRY_MS);
      if (force || (one & (1UL << i)) || !f.started || millis() - f.lastRun >= period) {
        f.ok = f.run();
        f.lastRun = millis();
        if (f.ok) f.okAt = f.lastRun;
        f.started = true;
        if (!f.ok && !f.quiet) Log::printf("[%s] fetch failed, retry in %lus\n", f.name, WEATHER_RETRY_MS / 1000);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(WORKER_IDLE_MS));
  }
}

}  // namespace

namespace DataService {

void begin() {
  mutex = xSemaphoreCreateMutex();
  bool ok = (radarSpare = Radar::allocFrame()) != nullptr;
  for (RadarFrame &f : radarShared.frames) ok = ok && (f.pixels = Radar::allocFrame()) != nullptr;
  if (!ok) Log::println("[radar] frame buffer allocation failed");
  // Core 0 alongside the Wi-Fi stack; the UI loop and web server run on core 1.
  xTaskCreatePinnedToCore(worker, "data", WORKER_STACK, nullptr, 1, nullptr, 0);
}

void refreshAll() { refreshRequested = true; }

void refresh(const char *feed) {
  for (size_t i = 0; i < sizeof(feeds) / sizeof(feeds[0]) && i < 32; ++i) {
    if (strcmp(feeds[i].name, feed) == 0) feedRequested |= 1UL << i;
  }
}

void lock() { xSemaphoreTake(mutex, portMAX_DELAY); }

void unlock() { xSemaphoreGive(mutex); }

const WeatherData &weather() { return weatherShared; }
const AirQualityData &air() { return airShared; }
const WarningList &warnings() { return warningsShared; }
const AuroraData &aurora() { return auroraShared; }
const RadarLoop &radar() { return radarShared; }
const FlightList &flights() { return flightsShared; }
const TempestData &tempest() { return tempestShared; }
const CarbonData &carbon() { return carbonShared; }
const HomelabData &homelab() { return homelabShared; }
const HaData &homeAssistant() { return haShared; }
TempestData &tempestMutable() { return tempestShared; }

uint8_t feedStatus(FeedAge *out, uint8_t max) {
  uint8_t n = 0;
  for (const Feed &f : feeds) {
    if (f.quiet || n >= max) continue;
    out[n++] = {f.name, f.okAt, f.ok};
  }
  return n;
}

}  // namespace DataService
