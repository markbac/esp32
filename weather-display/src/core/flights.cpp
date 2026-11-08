/**
 * @file    flights.cpp
 * @brief   adsb.lol client, adsbdb route lookup and route cache.
 */
#include "flights.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <algorithm>

#include "config.h"
#include "geo.h"
#include "https_get.h"

namespace {

constexpr uint8_t ROUTE_CACHE_SIZE = 32;  ///< Callsign -> route entries remembered.
FlightRoute routeCache[ROUTE_CACHE_SIZE]; ///< Includes negative results (known = false).
uint8_t routeCacheNext = 0;               ///< Round-robin replacement index.
char pendingCallsign[9] = "";             ///< Route requested by the UI, awaiting lookup.
char pendingPhotoHex[7] = "";             ///< Photo requested by the UI, awaiting lookup.

/** @brief Cached aircraft photo (JPEG bytes in PSRAM). */
struct Photo {
  char hex[7] = "";
  bool exists = false;        ///< false = looked up, no photo available.
  uint8_t *jpeg = nullptr;
  size_t len = 0;
  char credit[48] = "";
};
constexpr uint8_t PHOTO_CACHE = 6;
constexpr size_t PHOTO_MAX = 64 * 1024;
Photo photos[PHOTO_CACHE];
uint8_t photoNext = 0;

/** @brief planespotters.net requires a User-Agent with a contact URL. */
const char *const PHOTO_UA = "ESP32-WeatherStation/1.0 (+https://github.com/markbac/esp32)";

FlightList previous;  ///< Last poll, for carrying trails forward (data worker only).
SemaphoreHandle_t cacheMutex = nullptr;   ///< Guards the cache and pending request.

/** @brief RAII lock for the route cache (created on first use). */
struct CacheLock {
  CacheLock() {
    if (!cacheMutex) cacheMutex = xSemaphoreCreateMutex();
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
  }
  ~CacheLock() { xSemaphoreGive(cacheMutex); }
};

/** @brief Cache lookup; caller holds CacheLock. */
const FlightRoute *cached(const char *callsign) {
  for (const FlightRoute &r : routeCache) {
    if (r.callsign[0] && strcmp(r.callsign, callsign) == 0) return &r;
  }
  return nullptr;
}

/** @brief Copy a JSON string field into a fixed buffer, trimming trailing spaces. */
void copyTrim(char *dst, size_t size, const char *src) {
  strlcpy(dst, src ? src : "", size);
  for (int i = strlen(dst) - 1; i >= 0 && dst[i] == ' '; --i) dst[i] = '\0';
}

/**
 * @brief  Find or fetch the route for @p callsign.
 * @param  callsign  Trimmed callsign.
 * @param[out] route Filled from cache or adsbdb.
 */
void lookupRoute(const char *callsign, FlightRoute &route) {
  route = FlightRoute{};
  if (!callsign[0]) return;
  {
    CacheLock lock;
    if (const FlightRoute *r = cached(callsign)) {
      route = *r;
      return;
    }
  }

  JsonDocument filter;
  JsonObject fr = filter["response"]["flightroute"].to<JsonObject>();
  fr["airline"]["name"] = true;
  fr["origin"]["iata_code"] = true;
  fr["origin"]["icao_code"] = true;
  fr["origin"]["municipality"] = true;
  fr["destination"]["municipality"] = true;
  fr["destination"]["iata_code"] = true;
  fr["destination"]["icao_code"] = true;

  JsonDocument doc;
  const bool ok = Https::get(
      String("https://api.adsbdb.com/v0/callsign/") + callsign, "route",
      [&](HTTPClient &http) { return !deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter)); },
      false);
  // adsbdb answers 404 for unknown callsigns: cache that as "unknown" too.

  strlcpy(route.callsign, callsign, sizeof(route.callsign));
  JsonObject r = doc["response"]["flightroute"];
  if (ok && !r.isNull()) {
    route.known = true;
    const char *from = r["origin"]["iata_code"] | (r["origin"]["icao_code"] | "");
    const char *to = r["destination"]["iata_code"] | (r["destination"]["icao_code"] | "");
    strlcpy(route.from, from, sizeof(route.from));
    strlcpy(route.to, to, sizeof(route.to));
    strlcpy(route.airline, r["airline"]["name"] | "", sizeof(route.airline));
    strlcpy(route.fromCity, r["origin"]["municipality"] | "", sizeof(route.fromCity));
    strlcpy(route.toCity, r["destination"]["municipality"] | "", sizeof(route.toCity));
  }
  CacheLock lock;
  routeCache[routeCacheNext] = route;
  routeCacheNext = (routeCacheNext + 1) % ROUTE_CACHE_SIZE;
}

/** @brief Photo cache lookup; caller holds CacheLock. */
Photo *cachedPhoto(const char *hex) {
  for (Photo &p : photos) {
    if (p.hex[0] && strcmp(p.hex, hex) == 0) return &p;
  }
  return nullptr;
}

/** @brief Look up and download the photo for @p hex into the cache (data worker). */
void fetchPhoto(const char *hex) {
  JsonDocument filter;
  JsonObject ph = filter["photos"][0].to<JsonObject>();
  ph["thumbnail_large"]["src"] = true;
  ph["photographer"] = true;
  JsonDocument doc;
  Https::get(
      String("https://api.planespotters.net/pub/photos/hex/") + hex, "photo",
      [&](HTTPClient &http) { return !deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter)); },
      false, PHOTO_UA);
  const char *src = doc["photos"][0]["thumbnail_large"]["src"] | "";

  Photo p;
  strlcpy(p.hex, hex, sizeof(p.hex));
  if (src[0]) {
    uint8_t *buf = static_cast<uint8_t *>(heap_caps_malloc(PHOTO_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    size_t n = 0;
    const bool ok = buf && Https::get(
                               src, "photo",
                               [&](HTTPClient &http) {
                                 auto *s = http.getStreamPtr();
                                 const int expected = http.getSize();
                                 uint32_t last = millis();
                                 while (n < PHOTO_MAX && (http.connected() || s->available())) {
                                   const size_t a = s->available();
                                   if (a) {
                                     n += s->readBytes(buf + n, min(a, PHOTO_MAX - n));
                                     last = millis();
                                     if (expected > 0 && n >= static_cast<size_t>(expected)) break;
                                   } else if (millis() - last > 8000) {
                                     return false;
                                   } else {
                                     delay(2);
                                   }
                                 }
                                 return n > 4 && buf[0] == 0xFF && buf[1] == 0xD8;  // JPEG SOI marker.
                               },
                               true, PHOTO_UA);
    if (ok) {
      p.exists = true;
      p.jpeg = buf;
      p.len = n;
      snprintf(p.credit, sizeof(p.credit), "(c) %s", doc["photos"][0]["photographer"] | "planespotters.net");
    } else if (buf) {
      free(buf);
    }
  }
  Serial.printf("[photo] %s: %s (%u bytes)\n", hex, p.exists ? "found" : "none", static_cast<unsigned>(p.len));

  CacheLock lock;
  Photo &slot = photos[photoNext];
  if (slot.jpeg) free(slot.jpeg);
  slot = p;
  photoNext = (photoNext + 1) % PHOTO_CACHE;
}

/** @brief Copy trails from the previous poll and append each aircraft's last position. */
void carryTrails(FlightList &list) {
  for (int i = 0; i < list.count; ++i) {
    Aircraft &a = list.items[i];
    for (int j = 0; j < previous.count; ++j) {
      const Aircraft &p = previous.items[j];
      if (strcmp(p.hex, a.hex) != 0) continue;
      a.trailCount = p.trailCount;
      memcpy(a.trailDist, p.trailDist, sizeof(a.trailDist));
      memcpy(a.trailBearing, p.trailBearing, sizeof(a.trailBearing));
      if (a.trailCount == TRAIL_POINTS) {  // Drop the oldest.
        memmove(a.trailDist, a.trailDist + 1, (TRAIL_POINTS - 1) * sizeof(float));
        memmove(a.trailBearing, a.trailBearing + 1, (TRAIL_POINTS - 1) * sizeof(float));
        --a.trailCount;
      }
      a.trailDist[a.trailCount] = p.distNm;
      a.trailBearing[a.trailCount] = p.bearing;
      ++a.trailCount;
      break;
    }
  }
}

}  // namespace

namespace Flights {

bool fetch(FlightList &out) {
  // Keep only the fields we use; a busy sky can be 50+ KB of JSON.
  JsonDocument filter;
  JsonObject f = filter["ac"][0].to<JsonObject>();
  for (const char *k : {"hex", "flight", "r", "t", "desc", "alt_baro", "baro_rate", "gs", "track", "squawk", "lat", "lon", "dst", "dir"})
    f[k] = true;

  JsonDocument doc;
  const String url = String("https://api.adsb.lol/v2/point/") + WX_LATITUDE + "/" + WX_LONGITUDE + "/" + FLIGHT_RADIUS_NM;
  // adsb.lol requires HTTP/1.1 (answers 426 to 1.0), so read the whole body:
  // getString() decodes chunked encoding, and large Strings land in PSRAM.
  const bool ok = Https::get(
      url, "flights",
      [&](HTTPClient &http) {
        const DeserializationError err = deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter));
        if (err) Serial.printf("[flights] JSON error: %s\n", err.c_str());
        return !err;
      },
      false);
  if (!ok) return false;

  const double homeLat = atof(WX_LATITUDE), homeLon = atof(WX_LONGITUDE);
  FlightList list;
  JsonArray ac = doc["ac"];
  list.total = ac.size();

  // Collect all, then keep the nearest MAX_AIRCRAFT.
  for (JsonObject a : ac) {
    Aircraft p;
    copyTrim(p.hex, sizeof(p.hex), a["hex"] | "");
    copyTrim(p.callsign, sizeof(p.callsign), a["flight"] | "");
    copyTrim(p.desc, sizeof(p.desc), a["desc"] | "");
    copyTrim(p.squawk, sizeof(p.squawk), a["squawk"] | "");
    p.vertRate = static_cast<int16_t>(constrain(a["baro_rate"] | 0, -32000, 32000));
    copyTrim(p.reg, sizeof(p.reg), a["r"] | "");
    copyTrim(p.type, sizeof(p.type), a["t"] | "");
    if (a["alt_baro"].is<int>()) p.altFt = a["alt_baro"];
    else p.onGround = true;  // "ground" (string) or missing.
    p.speedKt = static_cast<uint16_t>(a["gs"] | 0.0f);
    p.track = static_cast<int16_t>(a["track"] | 0.0f);
    if (!a["lat"].is<double>()) continue;  // Position unknown: can't plot.
    const double lat = a["lat"], lon = a["lon"];
    p.distNm = a["dst"].is<float>() ? a["dst"].as<float>() : Geo::distanceNm(homeLat, homeLon, lat, lon);
    p.bearing = a["dir"].is<float>() ? a["dir"].as<float>() : Geo::bearingDeg(homeLat, homeLon, lat, lon);

    if (list.count < MAX_AIRCRAFT) {
      list.items[list.count++] = p;
    } else {
      // Replace the farthest stored aircraft if this one is nearer.
      Aircraft *far = std::max_element(list.items, list.items + list.count,
                                       [](const Aircraft &x, const Aircraft &y) { return x.distNm < y.distNm; });
      if (p.distNm < far->distNm) *far = p;
    }
  }
  std::sort(list.items, list.items + list.count, [](const Aircraft &x, const Aircraft &y) { return x.distNm < y.distNm; });

  for (int i = 0; i < list.count; ++i) {
    if (!list.items[i].onGround) {
      list.nearest = i;
      break;
    }
  }
  if (list.nearest >= 0) lookupRoute(list.items[list.nearest].callsign, list.route);
  carryTrails(list);
  previous = list;

  list.valid = true;
  list.fetchedAtMs = millis();
  out = list;

  if (list.nearest >= 0) {
    const Aircraft &n = list.items[list.nearest];
    Serial.printf("[flights] %u aircraft; nearest %s %s %ldft %ukt %.1fnm %s %s-%s\n", list.total, n.callsign, n.type,
                  static_cast<long>(n.altFt), n.speedKt, n.distNm, list.route.airline, list.route.from, list.route.to);
  } else {
    Serial.printf("[flights] %u aircraft, none airborne\n", list.total);
  }
  return true;
}

bool findRoute(const char *callsign, FlightRoute &route) {
  if (!callsign || !callsign[0]) return false;
  CacheLock lock;
  if (const FlightRoute *r = cached(callsign)) {
    route = *r;
    return true;
  }
  return false;
}

void requestRoute(const char *callsign) {
  if (!callsign || !callsign[0]) return;
  CacheLock lock;
  if (!cached(callsign)) strlcpy(pendingCallsign, callsign, sizeof(pendingCallsign));
}

void requestPhoto(const char *hex) {
  if (!hex || !hex[0]) return;
  CacheLock lock;
  if (!cachedPhoto(hex)) strlcpy(pendingPhotoHex, hex, sizeof(pendingPhotoHex));
}

int withPhoto(const char *hex, const std::function<void(const uint8_t *, size_t, const char *)> &fn) {
  if (!hex || !hex[0]) return 0;
  CacheLock lock;
  const Photo *p = cachedPhoto(hex);
  if (!p) return -1;
  if (!p->exists) return 0;
  fn(p->jpeg, p->len, p->credit);
  return 1;
}

bool resolvePending() {
  char hex[sizeof(pendingPhotoHex)] = "";
  {
    CacheLock lock;
    strlcpy(hex, pendingPhotoHex, sizeof(hex));
    pendingPhotoHex[0] = 0;
  }
  if (hex[0]) fetchPhoto(hex);

  char cs[sizeof(pendingCallsign)];
  {
    CacheLock lock;
    if (!pendingCallsign[0]) return true;
    strlcpy(cs, pendingCallsign, sizeof(cs));
    pendingCallsign[0] = '\0';
  }
  FlightRoute route;
  lookupRoute(cs, route);
  Serial.printf("[route] %s: %s %s-%s\n", cs, route.known ? route.airline : "unknown", route.from, route.to);
  return true;
}

}  // namespace Flights
