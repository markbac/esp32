/**
 * @file    geo.h
 * @brief   Geographic helpers: Web-Mercator tile maths and great-circle distance.
 */
#pragma once

#include <stdint.h>

namespace Geo {

/** @brief Edge length of a standard slippy-map tile, pixels. */
constexpr int TILE_SIZE = 256;

/**
 * @brief  Convert latitude/longitude to global Web-Mercator pixel coordinates.
 * @param  lat   Latitude, degrees.
 * @param  lon   Longitude, degrees.
 * @param  zoom  Slippy-map zoom level (world is 256 * 2^zoom pixels wide).
 * @param[out] x Global pixel x.
 * @param[out] y Global pixel y.
 */
void worldPixel(double lat, double lon, int zoom, double &x, double &y);

/** @brief Great-circle distance between two points, nautical miles. */
float distanceNm(double lat1, double lon1, double lat2, double lon2);

/** @brief Initial bearing from point 1 to point 2, degrees true (0-360). */
float bearingDeg(double lat1, double lon1, double lat2, double lon2);

}  // namespace Geo
