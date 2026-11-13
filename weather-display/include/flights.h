/**
 * @file    flights.h
 * @brief   Nearby aircraft from the adsb.lol ADS-B API, with route lookup.
 *
 * @details adsb.lol (https://api.adsb.lol) is a free, community-fed ADS-B
 *          aggregator (data licensed ODbL). Routes for the nearest aircraft come
 *          from adsbdb.com (https://api.adsbdb.com) and are cached by callsign.
 *
 *          The nearest aircraft's route is looked up on every poll. Routes for
 *          any other aircraft (e.g. one the user taps on the CrowPanel) are
 *          requested with requestRoute() and resolved by resolvePending() on
 *          the data worker; findRoute() reads the cache from any task.
 *
 *          Aircraft photos come from planespotters.net (free API; the
 *          photographer credit must be shown). requestPhoto() queues a lookup
 *          by ICAO hex and withPhoto() hands the JPEG bytes to a callback under
 *          a lock. Trails: each poll carries forward the previous positions of
 *          every aircraft still in range (matched by ICAO hex).
 */
#pragma once

#include <Arduino.h>

#include <functional>

/** @brief Previous positions kept per aircraft for trails (one per poll). */
constexpr uint8_t TRAIL_POINTS = 10;

/** @brief One aircraft seen near the configured location. */
struct Aircraft {
  char hex[7] = "";       ///< ICAO 24-bit address (stable id), e.g. "4ac9e7".
  char callsign[9] = "";  ///< Flight callsign, e.g. "EZY542T" (may be empty).
  char desc[28] = "";     ///< Type description, e.g. "AIRBUS A-320neo".
  int16_t vertRate = 0;   ///< Climb (+) / descent (-) rate, ft/min.
  char squawk[5] = "";    ///< Transponder code.
  char reg[9] = "";       ///< Registration, e.g. "G-EZWA".
  char type[5] = "";      ///< ICAO type designator, e.g. "A20N".
  int32_t altFt = 0;      ///< Barometric altitude, feet.
  bool onGround = false;  ///< Reported as on the ground.
  uint16_t speedKt = 0;   ///< Ground speed, knots.
  int16_t track = 0;      ///< Track over ground, degrees true.
  float distNm = 0;       ///< Distance from home, nautical miles.
  float bearing = 0;      ///< Bearing from home, degrees true.
  float lat = 0, lon = 0;  ///< Position, degrees.
  uint8_t trailCount = 0;                 ///< Valid trail points (oldest first).
  float trailDist[TRAIL_POINTS] = {};     ///< Earlier distances, nm.
  float trailBearing[TRAIL_POINTS] = {};  ///< Earlier bearings, degrees.
};

/** @brief Origin/destination for a callsign. */
struct FlightRoute {
  bool known = false;      ///< Route found in adsbdb.
  char callsign[9] = "";   ///< Callsign this route belongs to.
  char from[5] = "";       ///< Origin IATA (or ICAO) code.
  char to[5] = "";         ///< Destination IATA (or ICAO) code.
  char airline[28] = "";   ///< Airline name.
  char fromCity[24] = "";  ///< Origin municipality, e.g. "Copenhagen".
  char toCity[24] = "";    ///< Destination municipality.
};

/** @brief Maximum aircraft kept, nearest first (CrowPanel builds keep more for the list view). */
#ifndef MAX_AIRCRAFT
#define MAX_AIRCRAFT 24
#endif

/** @brief Result of one poll. */
struct FlightList {
  bool valid = false;               ///< False until the first successful fetch.
  uint32_t fetchedAtMs = 0;         ///< millis() at fetch time.
  Aircraft items[MAX_AIRCRAFT];     ///< Sorted by distance, nearest first.
  uint8_t count = 0;                ///< Entries in @ref items.
  uint16_t total = 0;               ///< Aircraft reported within range.
  uint16_t radiusNm = 25;           ///< Search radius used for this poll.
  int8_t nearest = -1;              ///< Index of nearest airborne aircraft, or -1.
  FlightRoute route;                ///< Route of @ref nearest (if known).
};

namespace Flights {

/**
 * @brief  Poll aircraft within the configured radius (Settings, default FLIGHT_RADIUS_NM) and look up the nearest one's route.
 * @param[out] out  Overwritten only on success.
 * @return true on success (an empty sky is a success).
 */
bool fetch(FlightList &out);

/**
 * @brief  Look up a cached route (thread-safe, never blocks on the network).
 * @param  callsign  Trimmed callsign.
 * @param[out] route Filled if cached (route.known may still be false = unknown).
 * @return true if the callsign is in the cache.
 */
bool findRoute(const char *callsign, FlightRoute &route);

/** @brief Ask the data worker to look up @p callsign's route soon (thread-safe). */
void requestRoute(const char *callsign);

/** @brief Ask the data worker to fetch the photo for ICAO @p hex (thread-safe). */
void requestPhoto(const char *hex);

/**
 * @brief  Run @p fn with the cached photo for @p hex while holding the cache lock.
 * @param  fn  Receives JPEG bytes, length and the photographer credit.
 * @return 1 if a photo was passed to @p fn, 0 if none exists, -1 if not looked up yet.
 */
int withPhoto(const char *hex, const std::function<void(const uint8_t *, size_t, const char *)> &fn);

/** @brief Resolve pending route and photo requests (called on the data worker). */
bool resolvePending();

}  // namespace Flights
