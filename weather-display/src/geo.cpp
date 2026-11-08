/**
 * @file    geo.cpp
 * @brief   Implementation of geographic helpers.
 */
#include "geo.h"

#include <math.h>

namespace {
constexpr double DEG = M_PI / 180.0;          ///< Degrees -> radians.
constexpr double EARTH_RADIUS_NM = 3440.065;  ///< Mean Earth radius, nautical miles.
}  // namespace

namespace Geo {

void worldPixel(double lat, double lon, int zoom, double &x, double &y) {
  const double worldSize = TILE_SIZE * static_cast<double>(1UL << zoom);
  const double latRad = lat * DEG;
  x = (lon + 180.0) / 360.0 * worldSize;
  y = (1.0 - asinh(tan(latRad)) / M_PI) / 2.0 * worldSize;
}

float distanceNm(double lat1, double lon1, double lat2, double lon2) {
  const double dLat = (lat2 - lat1) * DEG;
  const double dLon = (lon2 - lon1) * DEG;
  const double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * DEG) * cos(lat2 * DEG) * sin(dLon / 2) * sin(dLon / 2);
  return static_cast<float>(2 * EARTH_RADIUS_NM * atan2(sqrt(a), sqrt(1 - a)));
}

float bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  const double y = sin((lon2 - lon1) * DEG) * cos(lat2 * DEG);
  const double x = cos(lat1 * DEG) * sin(lat2 * DEG) - sin(lat1 * DEG) * cos(lat2 * DEG) * cos((lon2 - lon1) * DEG);
  double b = atan2(y, x) / DEG;
  if (b < 0) b += 360.0;
  return static_cast<float>(b);
}

}  // namespace Geo
