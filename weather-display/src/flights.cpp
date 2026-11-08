/**
 * @file    flights.cpp
 * @brief   adsb.lol client, adsbdb route lookup and route cache.
 */
#include "flights.h"

#include <ArduinoJson.h>

#include <algorithm>

#include "config.h"
#include "geo.h"
#include "https_get.h"

namespace {

constexpr uint8_t ROUTE_CACHE_SIZE = 12;  ///< Callsign -> route entries remembered.
FlightRoute routeCache[ROUTE_CACHE_SIZE]; ///< Includes negative results (known = false).
uint8_t routeCacheNext = 0;               ///< Round-robin replacement index.

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
  for (const FlightRoute &r : routeCache) {
    if (strcmp(r.callsign, callsign) == 0) {
      route = r;
      return;
    }
  }

  JsonDocument filter;
  JsonObject fr = filter["response"]["flightroute"].to<JsonObject>();
  fr["airline"]["name"] = true;
  fr["origin"]["iata_code"] = true;
  fr["origin"]["icao_code"] = true;
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
  }
  routeCache[routeCacheNext] = route;
  routeCacheNext = (routeCacheNext + 1) % ROUTE_CACHE_SIZE;
}

}  // namespace

namespace Flights {

bool fetch(FlightList &out) {
  // Keep only the fields we use; a busy sky can be 50+ KB of JSON.
  JsonDocument filter;
  JsonObject f = filter["ac"][0].to<JsonObject>();
  for (const char *k : {"flight", "r", "t", "alt_baro", "gs", "track", "lat", "lon", "dst", "dir"}) f[k] = true;

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
    copyTrim(p.callsign, sizeof(p.callsign), a["flight"] | "");
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

}  // namespace Flights
