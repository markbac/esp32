/**
 * @file    wifi_scan.cpp
 * @brief   Asynchronous Wi-Fi scans merged into a per-BSSID RSSI history.
 */
#include "wifi_scan.h"
#include "log.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <new>

#include "net_test.h"

namespace {

WifiScanData *shared = nullptr;      ///< In PSRAM (about 7 KB).
SemaphoreHandle_t mutex = nullptr;
bool scanBusy = false;
uint32_t lastStartMs = 0;
constexpr uint32_t FIRST_SCAN_MS = 20UL * 1000UL;  ///< Let start-up downloads settle first.
constexpr uint32_t SCAN_TIMEOUT_MS = 15UL * 1000UL;

struct Lock {
  Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex); }
};

bool ready() {
  if (shared) return true;
  void *mem = heap_caps_malloc(sizeof(WifiScanData), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mem) mem = malloc(sizeof(WifiScanData));
  if (!mem) return false;
  shared = new (mem) WifiScanData();
  mutex = xSemaphoreCreateMutex();
  return true;
}

/** @brief Find the entry for @p bssid, adding (or recycling the stalest) if new. */
WifiNet *slotFor(WifiScanData &d, const uint8_t *bssid) {
  for (uint8_t i = 0; i < d.count; ++i) {
    if (memcmp(d.nets[i].bssid, bssid, 6) == 0) return &d.nets[i];
  }
  WifiNet *n = nullptr;
  if (d.count < WIFI_MAX_NETS) {
    n = &d.nets[d.count++];
  } else {
    for (uint8_t i = 0; i < d.count; ++i) {  // Recycle the one unseen for longest (not seen this scan).
      if (d.nets[i].lastSeenScan < d.scans && (!n || d.nets[i].lastSeenScan < n->lastSeenScan)) n = &d.nets[i];
    }
    if (!n) return nullptr;
  }
  *n = WifiNet{};
  memset(n->hist, WIFI_NOT_SEEN, sizeof(n->hist));
  memcpy(n->bssid, bssid, 6);
  return n;
}

/** @brief Merge @p found scan results into the shared history. */
void merge(int found) {
  uint8_t joined[6] = {};
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) memcpy(joined, WiFi.BSSID(), 6);

  Lock l;
  WifiScanData &d = *shared;
  ++d.scans;
  d.head = (d.head + 1) % WIFI_HISTORY;
  if (d.samples < WIFI_HISTORY) ++d.samples;
  for (uint8_t i = 0; i < d.count; ++i) {
    d.nets[i].hist[d.head] = WIFI_NOT_SEEN;
    d.nets[i].rssi = WIFI_NOT_SEEN;
    d.nets[i].connected = false;
  }
  for (int i = 0; i < found; ++i) {
    const uint8_t *bssid = WiFi.BSSID(i);
    if (!bssid) continue;
    WifiNet *n = slotFor(d, bssid);
    if (!n) continue;
    const int8_t r = static_cast<int8_t>(constrain(WiFi.RSSI(i), -127, 0));
    strlcpy(n->ssid, WiFi.SSID(i).c_str(), sizeof(n->ssid));
    n->channel = WiFi.channel(i);
    n->auth = static_cast<uint8_t>(WiFi.encryptionType(i));
    if (r > n->rssi) n->rssi = r;  // Same BSSID twice in one scan: keep the stronger.
    n->hist[d.head] = n->rssi;
    n->lastSeenScan = d.scans;
    n->connected = connected && memcmp(bssid, joined, 6) == 0;
  }
  // Forget networks not seen for the whole history window; refresh each one's best.
  uint8_t keep = 0;
  for (uint8_t i = 0; i < d.count; ++i) {
    WifiNet &n = d.nets[i];
    if (d.scans - n.lastSeenScan >= WIFI_HISTORY) continue;
    n.best = WIFI_NOT_SEEN;
    for (uint8_t k = 0; k < d.samples; ++k) n.best = max(n.best, d.at(i, k));
    if (keep != i) d.nets[keep] = n;
    ++keep;
  }
  d.count = keep;
  d.lastFound = static_cast<uint8_t>(min(found, 255));
  d.lastScanMs = millis();
}

}  // namespace

namespace WifiScan {

void poll() {
  if (!ready()) return;
  const uint32_t now = millis();
  if (scanBusy) {
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING && now - lastStartMs < SCAN_TIMEOUT_MS) return;
    if (n >= 0) {
      merge(n);
      Log::printf("[wifiscan] %d access points\n", n);
      char row[96];
      snprintf(row, sizeof(row), "scan,%d,,,,,,,,,,,,,%d access points", WiFi.RSSI(), n);
      Log::net(row);
    } else {
      Log::printf("[wifiscan] scan failed (%d)\n", n);
    }
    WiFi.scanDelete();
    scanBusy = false;
    return;
  }
  // Only scan while joined: scanning during a reconnect attempt would delay it.
  if (WiFi.status() != WL_CONNECTED || now < FIRST_SCAN_MS) return;
  // A scan takes the radio off-channel for ~2 s, which would show up as lag in a test.
  if (NetTest::running() || NetTest::monitoring()) return;
  if (lastStartMs && now - lastStartMs < WIFI_SCAN_INTERVAL_MS) return;
  lastStartMs = now;
  // async, show hidden, active scan, 120 ms per channel (about 1.6 s for 13 channels).
  scanBusy = WiFi.scanNetworks(true, true, false, 120) == WIFI_SCAN_RUNNING;
}

bool scanning() { return scanBusy; }

void copy(WifiScanData &out) {
  if (!shared) {
    out.count = 0;
    out.scans = 0;
    out.samples = 0;
    out.lastScanMs = 0;
    return;
  }
  Lock l;
  out = *shared;
}

const char *authName(uint8_t auth) {
  static const char *const names[] = {"Open", "WEP", "WPA", "WPA2", "WPA/2", "Enterprise", "WPA3", "WPA2/3", "WAPI", "OWE"};
  return auth < sizeof(names) / sizeof(names[0]) ? names[auth] : "Secured";
}

}  // namespace WifiScan
