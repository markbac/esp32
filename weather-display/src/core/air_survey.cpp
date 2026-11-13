/**
 * @file    air_survey.cpp
 * @brief   Promiscuous-mode frame counting while connected.
 *
 * @details The callback runs in the Wi-Fi driver's task for every frame, so it
 *          only updates counters and two small open-addressing MAC sets under
 *          a spinlock. Sets are fixed-size; once full, new addresses are
 *          counted as "more than N" rather than tracked.
 */
#include "air_survey.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>

#include <algorithm>

#include "log.h"
#include "wifi_scan.h"

namespace {

portMUX_TYPE spin = portMUX_INITIALIZER_UNLOCKED;

constexpr int OUR_SLOTS = 64;    ///< Clients of our access point tracked (with traffic).
constexpr int OTHER_SLOTS = 128; ///< Other transmitters tracked.
constexpr int AP_SLOTS = 32;     ///< Access points heard.

struct State {
  uint32_t startMs = 0;
  uint32_t frames = 0, ourFrames = 0, retries = 0, airtimeUs = 0, ourAirtimeUs = 0, ourBytes = 0;
  uint32_t beacons = 0, beaconUs = 0, ourBeaconUs = 0;
  uint8_t ourAps = 0;
  AirTalker ours[OUR_SLOTS];
  uint16_t oursN = 0;
  uint8_t others[OTHER_SLOTS][6];
  uint16_t othersN = 0;
  uint8_t aps[AP_SLOTS][6];
  uint8_t apsN = 0;
};

State *st = nullptr;            ///< In PSRAM (~2.5 KB).
uint8_t bssid[6] = {};          ///< Our access point.
uint8_t family[WIFI_MAX_FAMILY][2];  ///< Our whole system's address prefixes (WifiScan::ownFamily).
uint8_t familyN = 0;
uint8_t self[6] = {};           ///< This panel's MAC (excluded).
bool listening = false;
bool timed = false;
uint32_t timedEndMs = 0, timedLenMs = 0;
AirStats lastTimedStats;

/** @brief Air time of one frame, microseconds (legacy or HT rate). */
uint32_t airtime(const wifi_pkt_rx_ctrl_t &rx) {
  float mbps = 1;
  if (rx.sig_mode == 0) {
    switch (rx.rate) {
      case 0x00: mbps = 1; break;
      case 0x01: case 0x05: mbps = 2; break;
      case 0x02: case 0x06: mbps = 5.5f; break;
      case 0x03: case 0x07: mbps = 11; break;
      case 0x08: mbps = 48; break;
      case 0x09: mbps = 24; break;
      case 0x0A: mbps = 12; break;
      case 0x0B: mbps = 6; break;
      case 0x0C: mbps = 54; break;
      case 0x0D: mbps = 36; break;
      case 0x0E: mbps = 18; break;
      case 0x0F: mbps = 9; break;
      default: mbps = 6;
    }
  } else {
    static const float ht20[8] = {6.5f, 13, 19.5f, 26, 39, 52, 58.5f, 65};
    mbps = ht20[rx.mcs & 7] * (rx.mcs > 7 ? 2 : 1) * (rx.cwb ? 2.08f : 1) * (rx.sgi ? 1.11f : 1);
  }
  const uint32_t preamble = rx.sig_mode == 0 && mbps <= 11 ? 192 : (rx.sig_mode == 0 ? 20 : 36);
  return preamble + static_cast<uint32_t>(rx.sig_len * 8 / mbps);
}

inline bool same(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }
inline bool group(const uint8_t *a) { return a[0] & 1; }  // Broadcast / multicast.
/** @brief Same physical router: its SSIDs (main, guest, IoT) differ only in the first/last byte. */
inline bool ourBox(const uint8_t *a) { return inFamily(a, family, familyN) || memcmp(a + 1, bssid + 1, 4) == 0; }

/** @brief Find or add @p mac in a MAC list; returns index or -1 when full. Caller holds the spinlock. */
int addMac(uint8_t (*set)[6], uint16_t &n, int cap, const uint8_t *mac) {
  for (int i = 0; i < n; ++i) {
    if (same(set[i], mac)) return i;
  }
  if (n >= cap) return -1;
  memcpy(set[n], mac, 6);
  return n++;
}

void onFrame(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  const auto *p = static_cast<const wifi_promiscuous_pkt_t *>(buf);
  const uint8_t *h = p->payload;
  if (p->rx_ctrl.sig_len < 24) return;
  const uint8_t fc0 = h[0], fc1 = h[1];
  const uint8_t *a1 = h + 4, *a2 = h + 10, *a3 = h + 16;
  const bool toDs = fc1 & 1, fromDs = fc1 & 2;
  const uint32_t us = airtime(p->rx_ctrl);
  const uint16_t len = p->rx_ctrl.sig_len;

  portENTER_CRITICAL(&spin);
  if (st) {
    ++st->frames;
    st->airtimeUs += us;
    if (fc1 & 0x08) ++st->retries;
    if (type == WIFI_PKT_MGMT && ((fc0 >> 4) & 0xF) == 8) {  // Beacon: a2 = AP.
      ++st->beacons;
      st->beaconUs += us;
      if (ourBox(a2)) st->ourBeaconUs += us;
      uint16_t n = st->apsN;
      const uint16_t before = n;
      addMac(st->aps, n, AP_SLOTS, a2);
      st->apsN = static_cast<uint8_t>(n);
      if (n > before && ourBox(a2)) ++st->ourAps;
    }
    // Which address is the access point and which the client?
    const uint8_t *ap = toDs && !fromDs ? a1 : (!toDs && fromDs ? a2 : a3);
    const uint8_t *client = toDs && !fromDs ? a2 : (!toDs && fromDs ? a1 : (same(a2, a3) ? a1 : a2));
    if (ourBox(ap)) {
      ++st->ourFrames;
      st->ourAirtimeUs += us;
      st->ourBytes += len;
      if (type == WIFI_PKT_DATA && !group(client) && !ourBox(client) && !same(client, self)) {
        int i = 0;
        for (; i < st->oursN; ++i) {
          if (same(st->ours[i].mac, client)) break;
        }
        if (i == st->oursN && st->oursN < OUR_SLOTS) memcpy(st->ours[st->oursN++].mac, client, 6);
        if (i < st->oursN) {
          ++st->ours[i].frames;
          st->ours[i].bytes += len;
        }
      }
    } else if (!group(a2) && !same(a2, self) && !ourBox(a2)) {
      addMac(st->others, st->othersN, OTHER_SLOTS, a2);
    }
  }
  portEXIT_CRITICAL(&spin);
}

void reset() {
  portENTER_CRITICAL(&spin);
  const uint32_t now = millis();
  memset(st, 0, sizeof(State));
  st->startMs = now;
  portEXIT_CRITICAL(&spin);
}

AirStats collect() {
  AirStats a;
  if (!st) return a;
  portENTER_CRITICAL(&spin);
  a.durationMs = millis() - st->startMs;
  a.frames = st->frames;
  a.ourFrames = st->ourFrames;
  a.retries = st->retries;
  a.airtimeUs = st->airtimeUs;
  a.ourAirtimeUs = st->ourAirtimeUs;
  a.ourBytes = st->ourBytes;
  a.devicesOurs = st->oursN;
  a.accessPoints = st->apsN;
  a.beacons = st->beacons;
  a.beaconAirtimeUs = st->beaconUs;
  a.ourBeaconAirtimeUs = st->ourBeaconUs;
  a.ourAccessPoints = st->ourAps;
  // Other transmitters, minus access points (their beacons count as transmitters too).
  uint16_t others = 0;
  for (int i = 0; i < st->othersN; ++i) {
    bool isAp = false;
    for (int k = 0; k < st->apsN && !isAp; ++k) isAp = same(st->others[i], st->aps[k]);
    if (!isAp) ++others;
  }
  a.devicesOther = others;
  AirTalker sorted[OUR_SLOTS];
  const uint16_t n = st->oursN;
  memcpy(sorted, st->ours, n * sizeof(AirTalker));
  portEXIT_CRITICAL(&spin);
  std::sort(sorted, sorted + n, [](const AirTalker &x, const AirTalker &y) { return x.bytes > y.bytes; });
  for (int i = 0; i < n; ++i) a.talkerBytes += sorted[i].bytes;
  a.topCount = static_cast<uint8_t>(min<int>(n, 5));
  for (int i = 0; i < a.topCount; ++i) a.top[i] = sorted[i];
  a.channel = static_cast<uint8_t>(WiFi.channel());
  return a;
}

bool begin() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!st) {
    st = static_cast<State *>(heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!st) return false;
  }
  memcpy(bssid, WiFi.BSSID(), 6);
  familyN = WifiScan::ownFamily(family);
  WiFi.macAddress(self);
  reset();
  wifi_promiscuous_filter_t f = {};
  f.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&f);
  esp_wifi_set_promiscuous_rx_cb(onFrame);
  if (esp_wifi_set_promiscuous(true) != ESP_OK) return false;
  listening = true;
  Log::printf("[air] listening on channel %d\n", static_cast<int>(WiFi.channel()));
  return true;
}

}  // namespace

namespace AirSurvey {

bool start() {
  timed = false;
  return listening || begin();
}

bool startTimed(uint32_t ms) {
  if (listening) return false;  // A monitor run owns the radio.
  if (!begin()) return false;
  timed = true;
  timedLenMs = ms;
  timedEndMs = millis() + ms;
  return true;
}

void stop() {
  if (!listening) return;
  esp_wifi_set_promiscuous(false);
  listening = false;
  timed = false;
}

void poll() {
  if (!listening || !timed || static_cast<int32_t>(millis() - timedEndMs) < 0) return;
  lastTimedStats = collect();
  stop();
  const AirStats &t = lastTimedStats;
  Log::printf("[air] %u s: %u devices on your network, %u others, %u APs (%u yours), busy %.0f%%, beacons %.0f%% "
              "(yours %.0f%%), retries %.0f%%\n",
              static_cast<unsigned>(t.durationMs / 1000), t.devicesOurs, t.devicesOther, t.accessPoints,
              t.ourAccessPoints, t.busyPct(), t.beaconPct(), t.ourBeaconPct(), t.retryPct());
  char row[200];
  snprintf(row, sizeof(row), "air,%d,,,,,,,,,,,,,%u,%u,%.0f,%.0f,,,channel %u; %u APs (%u yours); beacons %.0f%% of air",
           static_cast<int>(WiFi.RSSI()), t.devicesOurs, t.devicesOther, t.busyPct(), t.retryPct(), t.channel,
           t.accessPoints, t.ourAccessPoints, t.beaconPct());
  Log::net(row);
}

AirStats take() {
  AirStats a = collect();
  familyN = WifiScan::ownFamily(family);  // Pick up mesh nodes found since the survey started.
  if (st) reset();
  return a;
}

AirStats lastTimed() { return lastTimedStats; }

bool active() { return listening; }

uint8_t progress() {
  if (!listening || !timed || !timedLenMs) return 0;
  const uint32_t left = static_cast<int32_t>(timedEndMs - millis()) > 0 ? timedEndMs - millis() : 0;
  return static_cast<uint8_t>(100 - 100 * left / timedLenMs);
}

}  // namespace AirSurvey
