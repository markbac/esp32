/**
 * @file    astro.cpp
 * @brief   Implementation of the moon phase calculation.
 */
#include "astro.h"

#include <math.h>
#include <stdio.h>

namespace {
constexpr double SYNODIC_DAYS = 29.530588853;   ///< Mean new-moon to new-moon period.
constexpr time_t REF_NEW_MOON = 947182440;      ///< New moon 2000-01-06 18:14 UTC.
constexpr double DAY_S = 86400.0;

const char *const PHASE_NAMES[] = {"New moon",  "Waxing crescent", "First quarter", "Waxing gibbous",
                                   "Full moon", "Waning gibbous",  "Last quarter",  "Waning crescent"};
}  // namespace

namespace Astro {

MoonInfo moon(time_t t) {
  MoonInfo m;
  double age = fmod((static_cast<double>(t) - REF_NEW_MOON) / DAY_S, SYNODIC_DAYS);
  if (age < 0) age += SYNODIC_DAYS;
  m.ageDays = static_cast<float>(age);
  m.phase = static_cast<float>(age / SYNODIC_DAYS);
  m.illumination = static_cast<float>((1.0 - cos(2.0 * M_PI * m.phase)) / 2.0);
  m.name = PHASE_NAMES[static_cast<int>(m.phase * 8.0f + 0.5f) % 8];

  const double toFull = (m.phase < 0.5f ? 0.5 - m.phase : 1.5 - m.phase) * SYNODIC_DAYS;
  const double toNew = (1.0 - m.phase) * SYNODIC_DAYS;
  m.nextFull = t + static_cast<time_t>(toFull * DAY_S);
  m.nextNew = t + static_cast<time_t>(toNew * DAY_S);
  return m;
}

int minutesOfDay(const char *hhmm) {
  int h = 0, mi = 0;
  if (!hhmm || sscanf(hhmm, "%d:%d", &h, &mi) != 2 || h < 0 || h > 23 || mi < 0 || mi > 59) return -1;
  return h * 60 + mi;
}

}  // namespace Astro
