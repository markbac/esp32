/**
 * @file    astro.h
 * @brief   Moon phase calculation (offline) and small time helpers.
 *
 * @details The phase uses a mean synodic month from a reference new moon,
 *          accurate to within about half a day, which is plenty for a display.
 *          Sunrise and sunset come from the weather API instead.
 */
#pragma once

#include <stdint.h>
#include <time.h>

/** @brief Moon state at a given instant. */
struct MoonInfo {
  float phase = 0;          ///< 0 = new, 0.25 = first quarter, 0.5 = full, 0.75 = last quarter.
  float ageDays = 0;        ///< Days since the last new moon.
  float illumination = 0;   ///< Lit fraction of the disc, 0-1.
  const char *name = "";    ///< e.g. "Waxing gibbous".
  time_t nextFull = 0;      ///< Approximate next full moon (UTC epoch).
  time_t nextNew = 0;       ///< Approximate next new moon (UTC epoch).
};

namespace Astro {

/** @brief Moon phase information for UTC time @p t. */
MoonInfo moon(time_t t);

/**
 * @brief  Parse "HH:MM" into minutes after midnight.
 * @return Minutes, or -1 if the string is not a valid time.
 */
int minutesOfDay(const char *hhmm);

}  // namespace Astro
