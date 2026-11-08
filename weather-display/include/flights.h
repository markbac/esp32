/**
 * @file    flights.h
 * @brief   Nearby aircraft from the adsb.lol ADS-B API, with route lookup.
 *
 * @details adsb.lol (https://api.adsb.lol) is a free, community-fed ADS-B
 *          aggregator (data licensed ODbL). Routes for the nearest aircraft come
 *          from adsbdb.com (https://api.adsbdb.com) and are cached by callsign.
 */
#pragma once

#include <Arduino.h>

/** @brief One aircraft seen near the configured location. */
struct Aircraft {
  char callsign[9] = "";  ///< Flight callsign, e.g. "EZY542T" (may be empty).
  char reg[9] = "";       ///< Registration, e.g. "G-EZWA".
  char type[5] = "";      ///< ICAO type designator, e.g. "A20N".
  int32_t altFt = 0;      ///< Barometric altitude, feet.
  bool onGround = false;  ///< Reported as on the ground.
  uint16_t speedKt = 0;   ///< Ground speed, knots.
  int16_t track = 0;      ///< Track over ground, degrees true.
  float distNm = 0;       ///< Distance from home, nautical miles.
  float bearing = 0;      ///< Bearing from home, degrees true.
};

/** @brief Origin/destination for a callsign. */
struct FlightRoute {
  bool known = false;      ///< Route found in adsbdb.
  char callsign[9] = "";   ///< Callsign this route belongs to.
  char from[5] = "";       ///< Origin IATA (or ICAO) code.
  char to[5] = "";         ///< Destination IATA (or ICAO) code.
  char airline[28] = "";   ///< Airline name.
};

/** @brief Maximum aircraft kept, nearest first. */
constexpr uint8_t MAX_AIRCRAFT = 24;

/** @brief Result of one poll. */
struct FlightList {
  bool valid = false;               ///< False until the first successful fetch.
  uint32_t fetchedAtMs = 0;         ///< millis() at fetch time.
  Aircraft items[MAX_AIRCRAFT];     ///< Sorted by distance, nearest first.
  uint8_t count = 0;                ///< Entries in @ref items.
  uint16_t total = 0;               ///< Aircraft reported within range.
  int8_t nearest = -1;              ///< Index of nearest airborne aircraft, or -1.
  FlightRoute route;                ///< Route of @ref nearest (if known).
};

namespace Flights {

/**
 * @brief  Poll aircraft within FLIGHT_RADIUS_NM and look up the nearest one's route.
 * @param[out] out  Overwritten only on success.
 * @return true on success (an empty sky is a success).
 */
bool fetch(FlightList &out);

}  // namespace Flights
