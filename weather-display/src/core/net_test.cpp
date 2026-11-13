/**
 * @file    net_test.cpp
 * @brief   Ping (esp_ping), DNS and download tests run in a one-shot task.
 */
#include "net_test.h"
#include "log.h"

#include <WebServer.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/ip_addr.h>
#include <ping/ping_sock.h>

#include "https_get.h"
#include "wifi_scan.h"
#include <time.h>

namespace {

NetTestResult result;
SemaphoreHandle_t mutex = nullptr;
volatile bool busy = false;
volatile bool monStop = false;   ///< Ask the monitor task to finish.
volatile bool monBusy = false;   ///< Monitor task alive.

constexpr uint16_t PINGS = 20;
constexpr uint32_t PING_INTERVAL_MS = 250;
constexpr uint32_t PING_TIMEOUT_MS = 1000;
constexpr uint32_t DOWNLOAD_MS = 8000;  ///< Stop the download after this long.
const char *const DOWNLOAD_URL = "https://speed.cloudflare.com/__down?bytes=25000000";

struct Lock {
  Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex); }
};

void setStage(const char *stage, uint8_t progress) {
  Lock l;
  strlcpy(result.stage, stage, sizeof(result.stage));
  result.progress = progress;
}

/** @brief Collected while a ping session runs. */
struct PingRun {
  SemaphoreHandle_t done;
  uint32_t rtt[PINGS];
  uint16_t n = 0;
  uint16_t sent = 0;
};

void onReply(esp_ping_handle_t h, void *arg) {
  auto *run = static_cast<PingRun *>(arg);
  uint32_t t = 0;
  esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &t, sizeof(t));
  if (run->n < PINGS) run->rtt[run->n++] = t;
}

void onEnd(esp_ping_handle_t h, void *arg) {
  auto *run = static_cast<PingRun *>(arg);
  uint32_t sent = 0;
  esp_ping_get_profile(h, ESP_PING_PROF_REQUEST, &sent, sizeof(sent));
  run->sent = sent;
  xSemaphoreGive(run->done);
}

/** @brief Ping @p host (dotted quad) PINGS times. */
PingStats ping(const char *host) {
  PingStats s;
  ip_addr_t target;
  if (!ipaddr_aton(host, &target)) return s;
  PingRun run;
  run.done = xSemaphoreCreateBinary();
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr = target;
  cfg.count = PINGS;
  cfg.interval_ms = PING_INTERVAL_MS;
  cfg.timeout_ms = PING_TIMEOUT_MS;
  esp_ping_callbacks_t cbs = {};
  cbs.cb_args = &run;
  cbs.on_ping_success = onReply;
  cbs.on_ping_end = onEnd;
  esp_ping_handle_t h = nullptr;
  if (esp_ping_new_session(&cfg, &cbs, &h) == ESP_OK && esp_ping_start(h) == ESP_OK) {
    xSemaphoreTake(run.done, pdMS_TO_TICKS(PINGS * (PING_INTERVAL_MS + PING_TIMEOUT_MS) + 5000));
    esp_ping_stop(h);
    esp_ping_delete_session(h);
  }
  vSemaphoreDelete(run.done);

  s.sent = run.sent ? run.sent : PINGS;
  s.received = run.n;
  if (run.n) {
    uint32_t sum = 0, mn = UINT32_MAX, mx = 0, jit = 0;
    for (uint16_t i = 0; i < run.n; ++i) {
      sum += run.rtt[i];
      mn = min(mn, run.rtt[i]);
      mx = max(mx, run.rtt[i]);
      if (i) jit += run.rtt[i] > run.rtt[i - 1] ? run.rtt[i] - run.rtt[i - 1] : run.rtt[i - 1] - run.rtt[i];
    }
    s.minMs = mn;
    s.maxMs = mx;
    s.avgMs = static_cast<float>(sum) / run.n;
    s.jitterMs = run.n > 1 ? static_cast<float>(jit) / (run.n - 1) : 0;
  }
  Log::printf("[nettest] ping %s: %u/%u, avg %.0f ms (min %.0f max %.0f) jitter %.1f\n", host, s.received, s.sent,
                s.avgMs, s.minMs, s.maxMs, s.jitterMs);
  return s;
}

/** @brief Download for up to DOWNLOAD_MS and return Mbps (-1 on failure). */
float download(uint32_t &bytes) {
  bytes = 0;
  uint32_t t0 = 0, t1 = 0;
  static uint8_t buf[4096];  // Static: keeps the test task's stack small.
  Https::Options opt;
  opt.http10 = true;
  opt.timeoutMs = 8000;
  const bool ok = Https::request(
      "GET", DOWNLOAD_URL, "nettest", String(),
      [&](HTTPClient &http) {
        WiFiClient *s = http.getStreamPtr();
        t0 = millis();
        uint32_t last = t0;
        while (millis() - t0 < DOWNLOAD_MS && (s->connected() || s->available())) {
          const int a = s->available();
          if (a > 0) {
            bytes += s->readBytes(buf, min<size_t>(a, sizeof(buf)));
            last = millis();
            const uint8_t pct = 70 + 30 * (millis() - t0) / DOWNLOAD_MS;
            if ((bytes & 0xFFFF) < 4096) setStage("Download speed", min<uint8_t>(pct, 99));
            // TLS decryption is CPU-bound: yield every chunk or the idle task starves and the
            // task watchdog resets the board on a fast link.
            vTaskDelay(1);
          } else if (millis() - last > 3000) {
            break;
          } else {
            delay(1);
          }
        }
        t1 = millis();
        return bytes > 0;
      },
      opt);
  if (!ok || t1 <= t0) return -1;
  const float mbps = bytes * 8.0f / ((t1 - t0) / 1000.0f) / 1e6f;
  Log::printf("[nettest] download %lu bytes in %lu ms = %.1f Mbps\n", static_cast<unsigned long>(bytes),
                static_cast<unsigned long>(t1 - t0), mbps);
  return mbps;
}

void task(void *) {
  NetTestResult r;
  r.state = NetTestResult::State::Running;
  r.rssi = WiFi.RSSI();
  const String gw = WiFi.gatewayIP().toString();

  // A Wi-Fi scan in progress would add ~500 ms spikes; new scans are held off while we run.
  for (int i = 0; i < 60 && WifiScan::scanning(); ++i) delay(100);
  setStage("Ping router", 0);
  r.router = ping(gw.c_str());
  {
    Lock l;
    result.router = r.router;
  }
  setStage("Ping internet", 35);
  r.internet = ping("1.1.1.1");
  {
    Lock l;
    result.internet = r.internet;
  }
  setStage("DNS lookup", 68);
  IPAddress ip;
  const uint32_t d0 = millis();
  r.dnsMs = WiFi.hostByName("speed.cloudflare.com", ip) ? static_cast<float>(millis() - d0) : -1;
  setStage("Download speed", 70);
  r.downMbps = download(r.downBytes);
  if (r.downMbps < 0) {  // TLS connect can fail while other feeds hold memory; try once more.
    delay(2000);
    r.downMbps = download(r.downBytes);
  }

  r.state = NetTestResult::State::Done;
  r.finishedAtMs = millis();
  {
    Lock l;
    result = r;
  }
  Log::printf("[nettest] done: %s\n", r.verdict());
  {
    char row[200];
    snprintf(row, sizeof(row), "test,%d,%.0f,%.0f,%u,%u,%.0f,%.0f,%u,%u,,,%.1f,%s,jitter %.0f/%.0f ms dns %.0f ms", r.rssi,
             r.router.avgMs, r.router.maxMs, static_cast<unsigned>(r.router.sent - r.router.received), r.router.sent, r.internet.avgMs,
             r.internet.maxMs, static_cast<unsigned>(r.internet.sent - r.internet.received), r.internet.sent, r.downMbps, r.verdict(),
             r.router.jitterMs, r.internet.jitterMs, r.dnsMs);
    Log::net(row);
  }
  busy = false;
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------- monitor

MonitorData mon;                       ///< Guarded by mutex.

/** @brief Running sums for the minute in progress. */
struct Acc {
  uint32_t rSum = 0, rN = 0, iSum = 0, iN = 0, rssiN = 0;
  int32_t rssiSum = 0;
} acc;

/** @brief Ping callbacks for monitor sessions; arg = 0 router, 1 internet. */
void monReply(esp_ping_handle_t h, void *arg) {
  uint32_t t = 0;
  esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &t, sizeof(t));
  const bool inet = arg != nullptr;
  const uint16_t ms = static_cast<uint16_t>(min<uint32_t>(t, 65535));
  Lock l;
  MonitorMinute &m = mon.current;
  if (inet) {
    ++m.inetSent;
    ++mon.inetSent;
    acc.iSum += ms;
    ++acc.iN;
    m.inetMax = max(m.inetMax, ms);
  } else {
    ++m.routerSent;
    ++mon.routerSent;
    acc.rSum += ms;
    ++acc.rN;
    m.routerMax = max(m.routerMax, ms);
  }
  if (ms > MONITOR_SPIKE_MS) {
    if (m.spikes < 255) ++m.spikes;
    ++mon.spikes;
  }
  if (ms > mon.worstMs) {
    mon.worstMs = ms;
    mon.worstAt = time(nullptr);
  }
}

void monTimeout(esp_ping_handle_t, void *arg) {
  const bool inet = arg != nullptr;
  Lock l;
  MonitorMinute &m = mon.current;
  if (inet) {
    ++m.inetSent;
    ++m.inetLost;
    ++mon.inetSent;
    ++mon.inetLost;
  } else {
    ++m.routerSent;
    ++m.routerLost;
    ++mon.routerSent;
    ++mon.routerLost;
  }
}

esp_ping_handle_t monSession(const char *host, bool inet) {
  ip_addr_t target;
  if (!ipaddr_aton(host, &target)) return nullptr;
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr = target;
  cfg.count = ESP_PING_COUNT_INFINITE;
  cfg.interval_ms = 1000;
  cfg.timeout_ms = 1000;
  esp_ping_callbacks_t cbs = {};
  cbs.cb_args = inet ? reinterpret_cast<void *>(1) : nullptr;
  cbs.on_ping_success = monReply;
  cbs.on_ping_timeout = monTimeout;
  esp_ping_handle_t h = nullptr;
  if (esp_ping_new_session(&cfg, &cbs, &h) != ESP_OK) return nullptr;
  esp_ping_start(h);
  return h;
}

/** @brief Close the minute in progress into the log; caller holds the lock. */
void closeMinute() {
  MonitorMinute &m = mon.current;
  m.routerAvg = acc.rN ? acc.rSum / acc.rN : 0;
  m.inetAvg = acc.iN ? acc.iSum / acc.iN : 0;
  m.rssiAvg = acc.rssiN ? static_cast<int8_t>(acc.rssiSum / static_cast<int32_t>(acc.rssiN)) : 0;
  if (mon.count < MONITOR_MAX_MIN) mon.log[mon.count++] = m;
  char row[160];
  snprintf(row, sizeof(row), "monitor,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,,,minute %u of %u min, weakest %d dBm", m.rssiAvg,
           m.routerAvg, m.routerMax, m.routerLost, m.routerSent, m.inetAvg, m.inetMax, m.inetLost, m.inetSent, m.spikes,
           m.dropouts, mon.count, mon.durationMin, m.rssiMin);
  Log::net(row);
  Log::printf("[monitor] min %u: rssi %d router %u/%u ms lost %u inet %u/%u ms lost %u spikes %u drops %u\n", mon.count,
                m.rssiAvg, m.routerAvg, m.routerMax, m.routerLost, m.inetAvg, m.inetMax, m.inetLost, m.spikes, m.dropouts);
  m = MonitorMinute{};
  m.at = time(nullptr);
  acc = Acc{};
}

void monTask(void *) {
  esp_ping_handle_t r = nullptr, i = nullptr;
  bool wasUp = true;
  for (int k = 0; k < 60 && WifiScan::scanning(); ++k) delay(100);
  uint32_t minuteStart = millis();
  const uint32_t endAt = millis() + mon.durationMin * 60000UL;
  while (!monStop && static_cast<int32_t>(millis() - endAt) < 0) {
    const bool up = WiFi.status() == WL_CONNECTED;
    if (up && !r) {  // (Re)start pings once joined; the gateway may change after a reconnect.
      r = monSession(WiFi.gatewayIP().toString().c_str(), false);
      i = monSession("1.1.1.1", true);
    }
    {
      Lock l;
      if (wasUp && !up) {
        ++mon.current.dropouts;
        ++mon.dropouts;
        Log::println("[monitor] Wi-Fi dropped");
        Log::net("dropout,,,,,,,,,,,1,,,Wi-Fi disconnected during monitor");
      }
      if (up) {
        const int8_t s = static_cast<int8_t>(WiFi.RSSI());
        acc.rssiSum += s;
        ++acc.rssiN;
        if (!mon.current.rssiMin || s < mon.current.rssiMin) mon.current.rssiMin = s;
        if (!mon.rssiMin || s < mon.rssiMin) mon.rssiMin = s;
      }
      if (millis() - minuteStart >= 60000) {
        closeMinute();
        minuteStart += 60000;
      }
    }
    if (!up && r) {  // Pings would only time out while disconnected; count the drop-out instead.
      esp_ping_stop(r);
      esp_ping_delete_session(r);
      esp_ping_stop(i);
      esp_ping_delete_session(i);
      r = i = nullptr;
    }
    wasUp = up;
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  for (esp_ping_handle_t h : {r, i}) {
    if (!h) continue;
    esp_ping_stop(h);
    esp_ping_delete_session(h);
  }
  {
    Lock l;
    if (mon.current.routerSent || mon.current.dropouts) closeMinute();  // Keep a partial last minute.
    mon.running = false;
    mon.endedAtMs = millis();
  }
  Log::printf("[monitor] finished: %s\n", mon.verdict());
  {
    char row[160];
    snprintf(row, sizeof(row), "monitor_end,%d,,%u,%lu,%lu,,%u,%lu,%lu,%lu,%lu,,%s,%u of %u min", mon.rssiMin, mon.worstMs,
             static_cast<unsigned long>(mon.routerLost), static_cast<unsigned long>(mon.routerSent), mon.worstMs,
             static_cast<unsigned long>(mon.inetLost), static_cast<unsigned long>(mon.inetSent),
             static_cast<unsigned long>(mon.spikes), static_cast<unsigned long>(mon.dropouts), mon.verdict(), mon.count,
             mon.durationMin);
    Log::net(row);
  }
  monBusy = false;
  vTaskDelete(nullptr);
}

}  // namespace

const char *MonitorData::verdict() const {
  if (!routerSent && !dropouts) return "No data";
  const float rLoss = routerSent ? 100.0f * routerLost / routerSent : 0;
  const float iLoss = inetSent ? 100.0f * inetLost / inetSent : 0;
  const float spikeRate = routerSent + inetSent ? 100.0f * spikes / (routerSent + inetSent) : 0;
  if (dropouts > 0 || rLoss >= 5) return "Wi-Fi problem";
  if (rLoss >= 1 || spikeRate >= 5) return "Wi-Fi unstable";
  if (iLoss >= 2 || spikeRate >= 2) return "Internet issues";
  return "Healthy";
}

const char *NetTestResult::verdict() const {
  if (router.received == 0) return "Bad";
  const float loss = max(router.lossPct(), internet.lossPct());
  const float lat = router.avgMs, jit = max(router.jitterMs, internet.jitterMs);
  if (loss >= 10 || lat > 100 || jit > 50 || rssi < -82) return "Bad";
  if (loss >= 3 || lat > 30 || jit > 20 || rssi < -72) return "Poor";
  if (loss > 0 || lat > 10 || jit > 8 || rssi < -65) return "OK";
  return "Good";
}

namespace NetTest {

bool start() {
  if (!mutex) mutex = xSemaphoreCreateMutex();
  if (busy || monBusy || WiFi.status() != WL_CONNECTED) return false;
  busy = true;
  {
    Lock l;
    result = NetTestResult{};
    result.state = NetTestResult::State::Running;
    result.rssi = WiFi.RSSI();
    strlcpy(result.stage, "Starting", sizeof(result.stage));
  }
  // 12 KB: the TLS handshake needs most of it. Core 0, alongside the data worker.
  if (xTaskCreatePinnedToCore(task, "nettest", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
    busy = false;
    Lock l;
    result.state = NetTestResult::State::Idle;
    return false;
  }
  return true;
}

NetTestResult get() {
  if (!mutex) return NetTestResult{};
  Lock l;
  return result;
}

bool running() { return busy; }

bool startMonitor(uint8_t minutes) {
  if (!mutex) mutex = xSemaphoreCreateMutex();
  if (monBusy || busy || WiFi.status() != WL_CONNECTED) return false;
  monBusy = true;
  monStop = false;
  {
    Lock l;
    memset(&mon, 0, sizeof(mon));  // All-zero = defaults; avoids a 2.5 KB temporary on the caller's stack.
    mon.running = true;
    mon.durationMin = constrain(minutes, 1, MONITOR_MAX_MIN);
    mon.startedAtMs = millis();
    mon.current.at = time(nullptr);
    acc = Acc{};
  }
  Log::printf("[monitor] start, %u min\n", minutes);
  if (xTaskCreatePinnedToCore(monTask, "monitor", 4096, nullptr, 1, nullptr, 0) != pdPASS) {
    monBusy = false;
    Lock l;
    mon.running = false;
    return false;
  }
  return true;
}

void stopMonitor() { monStop = true; }

void registerRoutes(WebServer &server) {
  static WebServer *srv = &server;
  server.on("/api/nettest", HTTP_POST, [] {
    const String a = srv->arg("action");
    bool ok = false;
    if (a == "test") ok = start();
    else if (a == "monitor") ok = startMonitor(constrain(srv->arg("minutes").toInt(), 1, MONITOR_MAX_MIN));
    else if (a == "stop") ok = (stopMonitor(), true);
    srv->send(ok ? 200 : 409, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"busy or offline\"}");
  });
}

bool monitoring() { return monBusy; }

void monitor(MonitorData &out) {
  if (!mutex) {
    out.running = false;
    out.count = 0;
    out.startedAtMs = 0;
    return;
  }
  Lock l;
  out = mon;
}

}  // namespace NetTest
