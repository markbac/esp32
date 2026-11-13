/**
 * @file    air_survey.cpp
 * @brief   Promiscuous-mode frame accounting while connected, and GET /api/air.json.
 *
 * @details The callback runs in the Wi-Fi driver's task for every frame, so it
 *          only updates counters and small fixed-size tables under a spinlock.
 *          Snapshots are copied out under the spinlock into a PSRAM buffer and
 *          summarised outside it (a mutex serialises the UI loop, the monitor
 *          task and the web server).
 */
#include "air_survey.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <algorithm>

#include "log.h"
#include "wifi_scan.h"

namespace {

portMUX_TYPE spin = portMUX_INITIALIZER_UNLOCKED;

constexpr int OTHER_SLOTS = 128;  ///< Other transmitters tracked.
constexpr int AP_SLOTS = 32;      ///< Access-point addresses heard.
constexpr int SOURCE_SLOTS = 32;  ///< Broadcast sources tracked.

/** @brief Live counters, written by the frame callback. */
struct State {
  uint32_t startMs;
  uint32_t frames, retries, airtimeUs, beacons;
  uint32_t ourDataUs, ourGroupUs, backhaulUs, ourBeaconUs, ourMgmtUs, otherBeaconUs, otherUs;
  AirTalker talkers[AIR_MAX_TALKERS];
  uint16_t talkersN;
  AirUnit units[AIR_MAX_UNITS];
  uint64_t unitKeys[AIR_MAX_UNITS];
  uint8_t unitsN;
  uint8_t others[OTHER_SLOTS][6];
  uint16_t othersN;
  uint8_t aps[AP_SLOTS][6];
  uint8_t apsN, ourAps;
  uint32_t groupKindUs[static_cast<int>(GroupKind::Count)];
  AirSource sources[SOURCE_SLOTS];
  uint8_t sourcesN;
};

State *st = nullptr;              ///< Live counters (PSRAM).
State *snap = nullptr;            ///< Copy for summarising (PSRAM), guarded by snapMutex.
SemaphoreHandle_t snapMutex = nullptr;
AirTalker *lastList = nullptr;    ///< Full device list of the last timed survey (PSRAM).
uint8_t lastListN = 0;

uint8_t family[WIFI_MAX_FAMILY][2];  ///< Your system's address prefixes (WifiScan::ownFamily).
uint8_t familyN = 0;
uint8_t bssid[6] = {};               ///< Access point the panel is joined to.
uint8_t self[6] = {};                ///< This panel (excluded).
bool listening = false;
bool timed = false;
uint32_t timedEndMs = 0, timedLenMs = 0;
AirStats lastTimedStats;

/** @brief PHY rate of a frame, Mbps (legacy table or HT MCS). */
float rateMbps(const wifi_pkt_rx_ctrl_t &rx) {
  if (rx.sig_mode == 0) {
    switch (rx.rate) {
      case 0x00: return 1;
      case 0x01: case 0x05: return 2;
      case 0x02: case 0x06: return 5.5f;
      case 0x03: case 0x07: return 11;
      case 0x08: return 48;
      case 0x09: return 24;
      case 0x0A: return 12;
      case 0x0B: return 6;
      case 0x0C: return 54;
      case 0x0D: return 36;
      case 0x0E: return 18;
      case 0x0F: return 9;
      default: return 6;
    }
  }
  static const float ht20[8] = {6.5f, 13, 19.5f, 26, 39, 52, 58.5f, 65};
  return ht20[rx.mcs & 7] * (rx.mcs > 7 ? 2 : 1) * (rx.cwb ? 2.08f : 1) * (rx.sgi ? 1.11f : 1);
}

/** @brief Air time of one frame, microseconds: preamble + size / rate. */
uint32_t airtime(const wifi_pkt_rx_ctrl_t &rx, float mbps) {
  const uint32_t preamble = rx.sig_mode == 0 && mbps <= 11 ? 192 : (rx.sig_mode == 0 ? 20 : 36);
  return preamble + static_cast<uint32_t>(rx.sig_len * 8 / mbps);
}

inline bool same(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }
inline bool group(const uint8_t *a) { return a[0] & 1; }  // Broadcast / multicast.

/** @brief Kind of a group (broadcast/multicast) destination address. */
GroupKind kindOf(const uint8_t *d) {
  static const uint8_t mdns[6] = {0x01, 0x00, 0x5e, 0x00, 0x00, 0xfb};
  static const uint8_t ssdp[6] = {0x01, 0x00, 0x5e, 0x7f, 0xff, 0xfa};
  if ((d[0] & d[1] & d[2] & d[3] & d[4] & d[5]) == 0xff) return GroupKind::Broadcast;
  if (memcmp(d, mdns, 6) == 0 || (d[0] == 0x33 && d[1] == 0x33 && d[5] == 0xfb && d[4] == 0x00)) return GroupKind::Mdns;
  if (memcmp(d, ssdp, 6) == 0 || (d[0] == 0x33 && d[1] == 0x33 && d[4] == 0x00 && d[5] == 0x0c)) return GroupKind::Ssdp;
  if (d[0] == 0x33 && d[1] == 0x33) return GroupKind::Ipv6;
  return GroupKind::OtherMulticast;
}
inline bool ours(const uint8_t *a) { return inFamily(a, family, familyN) || memcmp(a + 1, bssid + 1, 4) == 0; }

/** @brief Find or add @p mac in a MAC list; returns index or -1 when full. Caller holds the spinlock. */
int addMac(uint8_t (*set)[6], uint16_t &n, int cap, const uint8_t *mac) {
  for (int i = 0; i < n; ++i) {
    if (same(set[i], mac)) return i;
  }
  if (n >= cap) return -1;
  memcpy(set[n], mac, 6);
  return n++;
}

/** @brief Index of the unit owning access-point address @p ap (added if new); 0xFF if the table is full. */
uint8_t unitFor(const uint8_t *ap) {
  const uint64_t k = radioKey(ap);
  for (uint8_t i = 0; i < st->unitsN; ++i) {
    if (st->unitKeys[i] == k) return i;
  }
  if (st->unitsN >= AIR_MAX_UNITS) return 0xFF;
  st->unitKeys[st->unitsN] = k;
  memcpy(st->units[st->unitsN].mac, ap, 6);
  return st->unitsN++;
}

/** @brief Talker entry for @p mac (added if new); nullptr when the table is full. */
AirTalker *talker(const uint8_t *mac) {
  for (uint16_t i = 0; i < st->talkersN; ++i) {
    if (same(st->talkers[i].mac, mac)) return &st->talkers[i];
  }
  if (st->talkersN >= AIR_MAX_TALKERS) return nullptr;
  AirTalker *t = &st->talkers[st->talkersN++];
  memcpy(t->mac, mac, 6);
  return t;
}

void onFrame(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  const auto *p = static_cast<const wifi_promiscuous_pkt_t *>(buf);
  const uint8_t *h = p->payload;
  if (p->rx_ctrl.sig_len < 24) return;
  const uint8_t fc0 = h[0], fc1 = h[1];
  const uint8_t *a1 = h + 4, *a2 = h + 10, *a3 = h + 16;
  const bool toDs = fc1 & 1, fromDs = fc1 & 2;
  const float mbps = rateMbps(p->rx_ctrl);
  const uint32_t us = airtime(p->rx_ctrl, mbps);
  const uint16_t len = p->rx_ctrl.sig_len;
  const int8_t rssi = p->rx_ctrl.rssi;
  const bool beacon = type == WIFI_PKT_MGMT && ((fc0 >> 4) & 0xF) == 8;

  portENTER_CRITICAL(&spin);
  if (!st) {
    portEXIT_CRITICAL(&spin);
    return;
  }
  ++st->frames;
  st->airtimeUs += us;
  if (fc1 & 0x08) ++st->retries;

  if (beacon) {  // a2 = the access point.
    ++st->beacons;
    uint16_t n = st->apsN;
    const uint16_t before = n;
    addMac(st->aps, n, AP_SLOTS, a2);
    st->apsN = static_cast<uint8_t>(n);
    if (ours(a2)) {
      st->ourBeaconUs += us;
      if (n > before) ++st->ourAps;
      const uint8_t u = unitFor(a2);
      if (u != 0xFF) st->units[u].rssi = rssi;
    } else {
      st->otherBeaconUs += us;
    }
  } else if (toDs && fromDs) {  // 4-address frame: one access point relaying to another.
    if (ours(a2) || ours(a1)) {
      st->backhaulUs += us;
      if (AirTalker *t = talker(a2)) {
        t->backhaul = true;
        ++t->frames;
        t->bytes += len;
        t->airtimeUs += us;
      }
    } else {
      st->otherUs += us;
    }
  } else {
    // Which address is the access point and which the device?
    const uint8_t *ap = toDs ? a1 : (fromDs ? a2 : a3);
    const uint8_t *dev = toDs ? a2 : (fromDs ? a1 : (same(a2, a3) ? a1 : a2));
    if (ours(ap)) {
      const uint8_t u = unitFor(ap);
      if (u != 0xFF) st->units[u].airtimeUs += us;
      if (type == WIFI_PKT_DATA && !group(dev) && !same(dev, self)) {
        const bool unitToUnit = ours(dev);
        if (unitToUnit) st->backhaulUs += us;
        else st->ourDataUs += us;
        if (AirTalker *t = talker(dev)) {
          t->backhaul = unitToUnit;
          ++t->frames;
          t->bytes += len;
          t->airtimeUs += us;
          t->unit = u;
          if (same(a2, dev)) {  // The device transmitted this frame: its signal and rate.
            t->rssiSum += rssi;
            ++t->rssiN;
            t->maxRate10 = max<uint16_t>(t->maxRate10, static_cast<uint16_t>(mbps * 10));
          }
        }
      } else if (type == WIFI_PKT_DATA && group(dev)) {
        st->ourGroupUs += us;
        st->groupKindUs[static_cast<int>(kindOf(dev))] += us;
        // From the AP to everyone: a3 is the device that sent it originally.
        const uint8_t *src = fromDs ? a3 : a2;
        if (!group(src)) {
          int i = 0;
          for (; i < st->sourcesN; ++i) {
            if (same(st->sources[i].mac, src)) break;
          }
          if (i == st->sourcesN && st->sourcesN < SOURCE_SLOTS) memcpy(st->sources[st->sourcesN++].mac, src, 6);
          if (i < st->sourcesN) {
            ++st->sources[i].frames;
            st->sources[i].airtimeUs += us;
          }
        }
      } else {
        st->ourMgmtUs += us;
      }
    } else {
      st->otherUs += us;
      if (!group(a2) && !same(a2, self) && !ours(a2)) addMac(st->others, st->othersN, OTHER_SLOTS, a2);
    }
  }
  portEXIT_CRITICAL(&spin);
}

void reset() {
  const uint32_t now = millis();
  portENTER_CRITICAL(&spin);
  memset(st, 0, sizeof(State));
  st->startMs = now;
  portEXIT_CRITICAL(&spin);
}

/**
 * @brief  Summarise the live counters.
 * @param  keepList  Also store the full device list as the "last timed survey" list.
 */
AirStats collect(bool keepList) {
  AirStats a;
  if (!st || !snap) return a;
  xSemaphoreTake(snapMutex, portMAX_DELAY);
  portENTER_CRITICAL(&spin);
  memcpy(snap, st, sizeof(State));
  portEXIT_CRITICAL(&spin);
  const State &s = *snap;
  a.durationMs = millis() - s.startMs;
  a.channel = static_cast<uint8_t>(WiFi.channel());
  a.frames = s.frames;
  a.retries = s.retries;
  a.airtimeUs = s.airtimeUs;
  a.ourDataUs = s.ourDataUs;
  a.ourGroupUs = s.ourGroupUs;
  for (int k = 0; k < static_cast<int>(GroupKind::Count); ++k) a.groupKindUs[k] = s.groupKindUs[k];
  {
    AirSource *src = snap->sources;
    std::sort(src, src + s.sourcesN, [](const AirSource &x, const AirSource &y) { return x.airtimeUs > y.airtimeUs; });
    a.sourceCount = static_cast<uint8_t>(min<int>(s.sourcesN, AIR_TOP_SOURCES));
    for (int i = 0; i < a.sourceCount; ++i) a.sources[i] = src[i];
  }
  a.backhaulUs = s.backhaulUs;
  a.ourBeaconUs = s.ourBeaconUs;
  a.ourMgmtUs = s.ourMgmtUs;
  a.otherBeaconUs = s.otherBeaconUs;
  a.otherUs = s.otherUs;
  a.accessPoints = s.apsN;
  a.ourAccessPoints = s.ourAps;
  a.beacons = s.beacons;
  // Other transmitters, minus access points.
  uint16_t others = 0;
  for (int i = 0; i < s.othersN; ++i) {
    bool isAp = false;
    for (int k = 0; k < s.apsN && !isAp; ++k) isAp = same(s.others[i], s.aps[k]);
    if (!isAp) ++others;
  }
  a.devicesOther = others;
  a.unitCount = s.unitsN;
  for (uint8_t i = 0; i < s.unitsN; ++i) a.units[i] = s.units[i];
  // Devices: busiest by air time first (sorted in the snapshot buffer).
  AirTalker *list = snap->talkers;
  const uint16_t n = s.talkersN;
  std::sort(list, list + n, [](const AirTalker &x, const AirTalker &y) { return x.airtimeUs > y.airtimeUs; });
  for (uint16_t i = 0; i < n; ++i) {
    if (list[i].backhaul) continue;
    ++a.devicesOurs;
    if (list[i].unit < a.unitCount) ++a.units[list[i].unit].clients;
    if (a.topCount < AIR_TOP) a.top[a.topCount++] = list[i];
  }
  a.talkers = n;
  if (keepList && lastList) {
    memcpy(lastList, list, n * sizeof(AirTalker));
    lastListN = static_cast<uint8_t>(n);
  }
  xSemaphoreGive(snapMutex);
  return a;
}

bool begin() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!st) {
    st = static_cast<State *>(heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    snap = static_cast<State *>(heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    lastList = static_cast<AirTalker *>(
        heap_caps_malloc(AIR_MAX_TALKERS * sizeof(AirTalker), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    snapMutex = xSemaphoreCreateMutex();
    if (!st || !snap || !lastList) return false;
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
  Log::printf("[air] listening on channel %d (%u mesh prefixes)\n", static_cast<int>(WiFi.channel()), familyN);
  return true;
}

/** @brief Write the summary of @p a into @p o (shared by the JSON log and /api/air.json). */
void summaryJson(const AirStats &a, JsonObject o) {
  o["seconds"] = a.durationMs / 1000;
  o["channel"] = a.channel;
  o["devices"] = a.devicesOurs;
  o["neighbours"] = a.devicesOther;
  o["accessPoints"] = a.accessPoints;
  o["yourBroadcasts"] = a.ourAccessPoints;
  o["busyPct"] = roundf(a.busyPct() * 10) / 10;
  o["retryPct"] = roundf(a.retryPct() * 10) / 10;
  JsonObject c = o["airPct"].to<JsonObject>();
  c["yourData"] = roundf(a.pct(a.ourDataUs) * 10) / 10;
  c["yourBroadcast"] = roundf(a.pct(a.ourGroupUs) * 10) / 10;
  c["meshBackhaul"] = roundf(a.pct(a.backhaulUs) * 10) / 10;
  c["yourBeacons"] = roundf(a.pct(a.ourBeaconUs) * 10) / 10;
  c["yourManagement"] = roundf(a.pct(a.ourMgmtUs) * 10) / 10;
  c["neighbourBeacons"] = roundf(a.pct(a.otherBeaconUs) * 10) / 10;
  c["other"] = roundf(a.pct(a.otherUs) * 10) / 10;
  JsonObject g = o["broadcastPctByKind"].to<JsonObject>();
  for (int k = 0; k < static_cast<int>(GroupKind::Count); ++k)
    g[AirSurvey::groupKindName(static_cast<GroupKind>(k))] = roundf(a.pct(a.groupKindUs[k]) * 10) / 10;
  JsonArray srcs = o["broadcastSources"].to<JsonArray>();
  for (uint8_t i = 0; i < a.sourceCount; ++i) {
    JsonObject s = srcs.add<JsonObject>();
    char m[18];
    AirSurvey::macStr(a.sources[i].mac, m);
    s["mac"] = m;
    s["airPct"] = roundf(a.pct(a.sources[i].airtimeUs) * 10) / 10;
    s["frames"] = a.sources[i].frames;
  }
  JsonArray units = o["units"].to<JsonArray>();
  for (uint8_t i = 0; i < a.unitCount; ++i) {
    JsonObject u = units.add<JsonObject>();
    char m[18];
    AirSurvey::macStr(a.units[i].mac, m);
    u["mac"] = m;
    u["rssi"] = a.units[i].rssi;
    u["devices"] = a.units[i].clients;
    u["airPct"] = roundf(a.pct(a.units[i].airtimeUs) * 10) / 10;
  }
}

/** @brief One device as JSON. */
void talkerJson(const AirStats &a, const AirTalker &t, JsonObject d) {
  char m[18];
  AirSurvey::macStr(t.mac, m);
  d["mac"] = m;
  d["airPct"] = roundf(a.pct(t.airtimeUs) * 10) / 10;
  d["kB"] = t.bytes / 1024;
  d["frames"] = t.frames;
  if (t.rssiN) d["rssi"] = t.rssi();
  if (t.maxRate10) d["maxMbps"] = t.maxRate10 / 10.0f;
  if (t.unit < a.unitCount) {
    char u[18];
    AirSurvey::macStr(a.units[t.unit].mac, u);
    d["via"] = u;
  }
  if (t.randomised()) d["private"] = true;
  if (t.backhaul) d["meshUnit"] = true;
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
  lastTimedStats = collect(true);
  stop();
  const AirStats &t = lastTimedStats;
  Log::printf("[air] %u s: %u devices on your Wi-Fi, %u others, %u units, busy %.0f%% (your data %.0f%%, backhaul %.0f%%, "
              "beacons %.0f%%, other %.0f%%), retries %.0f%%\n",
              static_cast<unsigned>(t.durationMs / 1000), t.devicesOurs, t.devicesOther, t.unitCount, t.busyPct(),
              t.pct(t.ourDataUs), t.pct(t.backhaulUs), t.beaconPct(), t.pct(t.otherUs), t.retryPct());
  JsonDocument doc;
  doc["type"] = "air";
  doc["rssi"] = WiFi.RSSI();
  summaryJson(t, doc.as<JsonObject>());
  JsonArray top = doc["top"].to<JsonArray>();
  for (uint8_t i = 0; i < lastListN && i < 10; ++i) talkerJson(t, lastList[i], top.add<JsonObject>());
  Log::net(doc);
}

AirStats take() {
  AirStats a = collect(false);
  familyN = WifiScan::ownFamily(family);  // Pick up mesh units found since the survey started.
  if (st) reset();
  return a;
}

AirStats lastTimed() { return lastTimedStats; }

uint8_t lastTalkers(AirTalker *out, uint8_t max) {
  if (!snapMutex || !lastList) return 0;
  xSemaphoreTake(snapMutex, portMAX_DELAY);
  const uint8_t n = min(max, lastListN);
  memcpy(out, lastList, n * sizeof(AirTalker));
  xSemaphoreGive(snapMutex);
  return n;
}

bool active() { return listening; }

uint8_t progress() {
  if (!listening || !timed || !timedLenMs) return 0;
  const uint32_t left = static_cast<int32_t>(timedEndMs - millis()) > 0 ? timedEndMs - millis() : 0;
  return static_cast<uint8_t>(100 - 100 * left / timedLenMs);
}

const char *groupKindName(GroupKind k) {
  switch (k) {
    case GroupKind::Broadcast: return "broadcast/ARP";
    case GroupKind::Mdns: return "mDNS";
    case GroupKind::Ssdp: return "SSDP/UPnP";
    case GroupKind::Ipv6: return "IPv6 multicast";
    default: return "other multicast";
  }
}

void macStr(const uint8_t *m, char *buf) {
  snprintf(buf, 18, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

void registerRoutes(WebServer &server) {
  static WebServer *srv = &server;
  server.on("/api/air.json", HTTP_GET, [] {
    const AirStats a = lastTimedStats;
    JsonDocument doc;
    if (!a.durationMs) {
      doc["error"] = "no survey yet: POST /api/nettest action=air, or Network > Crowding > Listen 20 s";
    } else {
      summaryJson(a, doc.to<JsonObject>());
      doc["note"] = "airPct = estimated share of air time (lower bound). 2.4 GHz only. 'private' = randomised MAC "
                    "(no vendor). 'meshUnit' = one of your access points relaying for others.";
      static AirTalker *list = static_cast<AirTalker *>(
          heap_caps_malloc(AIR_MAX_TALKERS * sizeof(AirTalker), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      const uint8_t n = list ? lastTalkers(list, AIR_MAX_TALKERS) : 0;
      JsonArray devs = doc["clients"].to<JsonArray>();
      for (uint8_t i = 0; i < n; ++i) talkerJson(a, list[i], devs.add<JsonObject>());
    }
    String out;
    serializeJson(doc, out);
    srv->sendHeader("Cache-Control", "no-store");
    srv->send(200, "application/json", out);
  });
}

}  // namespace AirSurvey
