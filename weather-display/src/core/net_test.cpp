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

#include "air_survey.h"
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
    snprintf(row, sizeof(row), "test,%d,%.0f,%.0f,%u,%u,%.0f,%.0f,%u,%u,,,%.1f,%s,,,,,,,jitter %.0f/%.0f ms dns %.0f ms", r.rssi,
             r.router.avgMs, r.router.maxMs, static_cast<unsigned>(r.router.sent - r.router.received), r.router.sent, r.internet.avgMs,
             r.internet.maxMs, static_cast<unsigned>(r.internet.sent - r.internet.received), r.internet.sent, r.downMbps, r.verdict(),
             r.router.jitterMs, r.internet.jitterMs, r.dnsMs);
    Log::net(row);
  }
  busy = false;
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------- monitor

MonitorData *monp = nullptr;           ///< In PSRAM; guarded by mutex.
#define mon (*monp)

/** @brief Running sums for the minute in progress. */
struct Acc {
  uint32_t rSum = 0, rN = 0, iSum = 0, iN = 0, rssiN = 0;
  int32_t rssiSum = 0;
} acc;

volatile int lastDisconnectReason = 0;  ///< From the Wi-Fi event, for drop-out events.
bool reasonHooked = false;

/** @brief Hostnames cycled for the per-minute DNS check, so lookups are rarely served from cache. */
const char *const DNS_NAMES[] = {"www.bbc.co.uk",    "www.google.com",  "www.wikipedia.org", "www.cloudflare.com",
                                 "www.github.com",   "www.amazon.co.uk", "www.microsoft.com", "www.apple.com",
                                 "www.youtube.com",  "www.netflix.com"};

/** @brief Record an event (and log it); caller holds the lock. */
void event(MonitorEventType type, int a = 0, int b = 0) {
  MonitorEvent e;
  e.at = time(nullptr);
  e.type = type;
  e.a = static_cast<int16_t>(constrain(a, -32768, 32767));
  e.b = static_cast<int16_t>(constrain(b, -32768, 32767));
  if (mon.eventCount == MONITOR_MAX_EVENTS) {
    memmove(mon.events, mon.events + 1, (MONITOR_MAX_EVENTS - 1) * sizeof(MonitorEvent));
    --mon.eventCount;
  }
  mon.events[mon.eventCount++] = e;
  char d[80], row[120];
  NetTest::describe(e, d, sizeof(d));
  Log::printf("[monitor] event: %s\n", d);
  snprintf(row, sizeof(row), "event,%d,,,,,,,,,,,,,,,,,,,%s", static_cast<int>(WiFi.RSSI()), d);
  Log::net(row);
}

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
void closeMinute(const AirStats &air) {
  MonitorMinute &m = mon.current;
  m.routerAvg = acc.rN ? acc.rSum / acc.rN : 0;
  m.inetAvg = acc.iN ? acc.iSum / acc.iN : 0;
  m.rssiAvg = acc.rssiN ? static_cast<int8_t>(acc.rssiSum / static_cast<int32_t>(acc.rssiN)) : 0;
  if (air.durationMs) {
    mon.airSurvey = true;
    m.devices = static_cast<uint8_t>(min<int>(air.devicesOurs, 255));
    m.otherDevices = static_cast<uint8_t>(min<int>(air.devicesOther, 255));
    m.busyPct = static_cast<uint8_t>(air.busyPct());
    m.retryPct = static_cast<uint8_t>(air.retryPct());
    mon.devicesMax = max(mon.devicesMax, m.devices);
    mon.busyMax = max(mon.busyMax, m.busyPct);
  }
  // Events for the minute (at most one of each kind, so the list stays readable).
  const bool prevWeak = mon.count && mon.log[mon.count - 1].rssiMin && mon.log[mon.count - 1].rssiMin < -80;
  if (m.routerMax > 500 || m.inetMax > 500)
    event(MonitorEventType::Spike, max(m.routerMax, m.inetMax), m.routerMax >= m.inetMax ? 0 : 1);
  if (m.routerLost >= 3 || m.inetLost >= 3)
    event(MonitorEventType::Loss, max(m.routerLost, m.inetLost), m.routerLost >= m.inetLost ? 0 : 1);
  if (m.rssiMin && m.rssiMin < -80 && !prevWeak) event(MonitorEventType::Weak, m.rssiMin);
  if (m.busyPct >= 60) event(MonitorEventType::Busy, m.busyPct, m.devices);
  if (m.retryPct >= 25 && air.frames > 200) event(MonitorEventType::Retries, m.retryPct);

  if (mon.count < MONITOR_MAX_MIN) mon.log[mon.count++] = m;
  char row[220];
  snprintf(row, sizeof(row),
           "monitor,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,,,%u,%u,%u,%u,%s%u,%u,minute %u of %u; weakest %d dBm", m.rssiAvg,
           m.routerAvg, m.routerMax, m.routerLost, m.routerSent, m.inetAvg, m.inetMax, m.inetLost, m.inetSent, m.spikes,
           m.dropouts, m.devices, m.otherDevices, m.busyPct, m.retryPct, m.dnsMs == 0xFFFF ? "-" : "",
           static_cast<unsigned>(m.dnsMs == 0xFFFF ? 0 : m.dnsMs), m.roams, mon.count, mon.durationMin, m.rssiMin);
  Log::net(row);
  Log::printf("[monitor] min %u: rssi %d router %u/%u ms lost %u inet %u/%u ms lost %u spikes %u drops %u devices %u "
              "busy %u%% retry %u%% dns %u\n",
              mon.count, m.rssiAvg, m.routerAvg, m.routerMax, m.routerLost, m.inetAvg, m.inetMax, m.inetLost, m.spikes,
              m.dropouts, m.devices, m.busyPct, m.retryPct, m.dnsMs);
  m = MonitorMinute{};
  m.at = time(nullptr);
  acc = Acc{};
}

void monTask(void *) {
  esp_ping_handle_t r = nullptr, i = nullptr;
  bool wasUp = true;
  uint32_t downAt = 0;
  uint8_t lastBssid[6] = {};
  int lastChannel = 0;
  uint8_t dnsIdx = 0;
  bool dnsDone = false;
  for (int k = 0; k < 60 && WifiScan::scanning(); ++k) delay(100);
  AirSurvey::stop();  // A timed survey from the Crowding page would otherwise own the radio.
  WifiScan::ensureScanned();  // Before any pings: identifies your mesh nodes for the air figures.
  const bool air = AirSurvey::start();
  if (WiFi.status() == WL_CONNECTED) {
    memcpy(lastBssid, WiFi.BSSID(), 6);
    lastChannel = WiFi.channel();
  }
  uint32_t minuteStart = millis();
  const uint32_t endAt = millis() + mon.durationMin * 60000UL;
  while (!monStop && static_cast<int32_t>(millis() - endAt) < 0) {
    const bool up = WiFi.status() == WL_CONNECTED;
    if (up && !r) {  // (Re)start pings once joined; the gateway may change after a reconnect.
      r = monSession(WiFi.gatewayIP().toString().c_str(), false);
      i = monSession("1.1.1.1", true);
      if (air && !AirSurvey::active()) AirSurvey::start();
    }
    // DNS check once a minute, half way through (outside the lock: it can take seconds).
    uint16_t dns = 0;
    if (up && !dnsDone && millis() - minuteStart >= 30000) {
      IPAddress ip;
      const uint32_t t0 = millis();
      dns = WiFi.hostByName(DNS_NAMES[dnsIdx++ % (sizeof(DNS_NAMES) / sizeof(DNS_NAMES[0]))], ip)
                ? static_cast<uint16_t>(max<uint32_t>(1, millis() - t0))
                : 0xFFFF;
      dnsDone = true;
    }
    const bool minuteDue = millis() - minuteStart >= 60000;
    const AirStats airMin = minuteDue && air ? AirSurvey::take() : AirStats{};
    {
      Lock l;
      if (dns) {
        mon.current.dnsMs = dns;
        if (dns == 0xFFFF) {
          ++mon.dnsFails;
          event(MonitorEventType::DnsFail);
        }
      }
      if (wasUp && !up) {
        ++mon.current.dropouts;
        ++mon.dropouts;
        downAt = millis();
        event(MonitorEventType::Drop, lastDisconnectReason);
      } else if (!wasUp && up) {
        event(MonitorEventType::Back, (millis() - downAt) / 1000);
      }
      if (up) {
        const int8_t s = static_cast<int8_t>(WiFi.RSSI());
        acc.rssiSum += s;
        ++acc.rssiN;
        if (!mon.current.rssiMin || s < mon.current.rssiMin) mon.current.rssiMin = s;
        if (!mon.rssiMin || s < mon.rssiMin) mon.rssiMin = s;
        const uint8_t *b = WiFi.BSSID();
        const int ch = WiFi.channel();
        if (b && lastChannel && (memcmp(b, lastBssid, 6) != 0 || ch != lastChannel)) {
          ++mon.current.roams;
          ++mon.roams;
          event(MonitorEventType::Roam, lastChannel, ch);
        }
        if (b) memcpy(lastBssid, b, 6);
        lastChannel = ch;
      }
      if (minuteDue) {
        closeMinute(airMin);
        minuteStart += 60000;
        dnsDone = false;
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
  const AirStats last = air ? AirSurvey::take() : AirStats{};
  AirSurvey::stop();
  {
    Lock l;
    if (mon.current.routerSent || mon.current.dropouts) closeMinute(last);  // Keep a partial last minute.
    event(MonitorEventType::End, mon.count);
    mon.running = false;
    mon.endedAtMs = millis();
  }
  Log::printf("[monitor] finished: %s\n", mon.verdict());
  {
    char row[220];
    snprintf(row, sizeof(row), "monitor_end,%d,,%u,%lu,%lu,,%u,%lu,%lu,%lu,%lu,,%s,%u,,%u,,%lu,%lu,%u of %u min",
             mon.rssiMin, mon.worstMs, static_cast<unsigned long>(mon.routerLost),
             static_cast<unsigned long>(mon.routerSent), mon.worstMs, static_cast<unsigned long>(mon.inetLost),
             static_cast<unsigned long>(mon.inetSent), static_cast<unsigned long>(mon.spikes),
             static_cast<unsigned long>(mon.dropouts), mon.verdict(), mon.devicesMax, mon.busyMax,
             static_cast<unsigned long>(mon.dnsFails), static_cast<unsigned long>(mon.roams), mon.count, mon.durationMin);
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
  if (rLoss >= 1 || spikeRate >= 5) return busyMax >= 60 ? "Wi-Fi congested" : "Wi-Fi unstable";
  if (iLoss >= 2 || spikeRate >= 2 || dnsFails > 2) return "Internet issues";
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

bool startMonitor(uint16_t minutes) {
  if (!mutex) mutex = xSemaphoreCreateMutex();
  if (monBusy || busy || WiFi.status() != WL_CONNECTED) return false;
  if (!monp) {
    monp = static_cast<MonitorData *>(heap_caps_malloc(sizeof(MonitorData), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!monp) return false;
  }
  if (!reasonHooked) {
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) { lastDisconnectReason = info.wifi_sta_disconnected.reason; },
                 ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    reasonHooked = true;
  }
  monBusy = true;
  monStop = false;
  {
    Lock l;
    memset(monp, 0, sizeof(MonitorData));  // All-zero = defaults (17 KB: never build a temporary).
    mon.running = true;
    mon.durationMin = constrain(minutes, 1, MONITOR_MAX_MIN);
    mon.startedAtMs = millis();
    mon.current.at = time(nullptr);
    acc = Acc{};
    event(MonitorEventType::Start, mon.durationMin);
  }
  Log::printf("[monitor] start, %u min\n", minutes);
  if (xTaskCreatePinnedToCore(monTask, "monitor", 6144, nullptr, 1, nullptr, 0) != pdPASS) {
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
    else if (a == "air") ok = !busy && !monBusy && AirSurvey::startTimed(20000);
    else if (a == "stop") ok = (stopMonitor(), true);
    srv->send(ok ? 200 : 409, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"busy or offline\"}");
  });
}

bool monitoring() { return monBusy; }

void monitor(MonitorData &out) {
  if (!mutex || !monp) {
    out.running = false;
    out.count = 0;
    out.eventCount = 0;
    out.startedAtMs = 0;
    return;
  }
  Lock l;
  memcpy(&out, monp, sizeof(MonitorData));
}

const char *reasonName(int r) {
  switch (r) {
    case 0: return "unknown";
    case 1: return "unspecified";
    case 2: return "auth expired";
    case 3: return "router ended it";
    case 4: return "inactive too long";
    case 5: return "router full";
    case 6: case 7: return "not authenticated";
    case 8: return "left";
    case 15: return "key handshake timeout";
    case 23: return "802.1X auth failed";
    case 200: return "beacon timeout (signal lost)";
    case 201: return "router not found";
    case 202: return "auth failed";
    case 203: return "association failed";
    case 204: return "handshake timeout";
    case 205: return "connection failed";
    default: return "other";
  }
}

void describe(const MonitorEvent &e, char *buf, size_t n) {
  char t[6];
  tm lt;
  localtime_r(&e.at, &lt);
  snprintf(t, sizeof(t), "%02d:%02d", lt.tm_hour, lt.tm_min);
  const char *who = e.b ? "internet" : "router";
  switch (e.type) {
    case MonitorEventType::Start: snprintf(buf, n, "%s Monitor started for %d min", t, e.a); break;
    case MonitorEventType::End: snprintf(buf, n, "%s Monitor ended after %d min", t, e.a); break;
    case MonitorEventType::Drop: snprintf(buf, n, "%s Wi-Fi dropped: %s (%d)", t, reasonName(e.a), e.a); break;
    case MonitorEventType::Back: snprintf(buf, n, "%s Wi-Fi back after %d s", t, e.a); break;
    case MonitorEventType::Roam: snprintf(buf, n, "%s Moved access point (ch %d to %d)", t, e.a, e.b); break;
    case MonitorEventType::Spike: snprintf(buf, n, "%s Lag spike %d ms to the %s", t, e.a, who); break;
    case MonitorEventType::Loss: snprintf(buf, n, "%s %d pings lost to the %s in a minute", t, e.a, who); break;
    case MonitorEventType::DnsFail: snprintf(buf, n, "%s DNS lookup failed", t); break;
    case MonitorEventType::Weak: snprintf(buf, n, "%s Weak signal %d dBm", t, e.a); break;
    case MonitorEventType::Busy: snprintf(buf, n, "%s Channel %d%% busy (%d devices on router)", t, e.a, e.b); break;
    case MonitorEventType::Retries: snprintf(buf, n, "%s %d%% of frames re-sent (interference)", t, e.a); break;
  }
}

}  // namespace NetTest
