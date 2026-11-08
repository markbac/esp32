/**
 * @file    aurora.h
 * @brief   AuroraWatch UK geomagnetic activity alerts (Lancaster University).
 *
 * @details API: https://aurorawatch-api.lancs.ac.uk/0.2/status/
 *          - current-status.xml          overall UK alert level
 *          - alerting-site-activity.xml  last 24 hourly activity values (nT)
 *
 *          The server caches for 180 s; please poll no more often than that
 *          (AURORA_INTERVAL_MS defaults to 5 minutes). Attribution:
 *          "AuroraWatch UK, Lancaster University".
 *
 *          Levels (activity thresholds): green < 50 nT, yellow 50-99,
 *          amber 100-199 (aurora possible from northern England), red >= 200.
 */
#pragma once

#include <Arduino.h>

/** @brief AuroraWatch UK alert levels in increasing severity. */
enum class AuroraLevel : uint8_t { Unknown, Green, Yellow, Amber, Red };

/** @brief Hourly activity samples kept (one day). */
constexpr uint8_t AURORA_HOURS = 24;

/** @brief Latest AuroraWatch status. */
struct AuroraData {
  bool valid = false;                       ///< False until the first successful fetch.
  uint32_t fetchedAtMs = 0;                 ///< millis() at fetch time.
  AuroraLevel level = AuroraLevel::Unknown; ///< Current UK alert level.
  float activity[AURORA_HOURS] = {};        ///< Hourly disturbance, nT (oldest first).
  AuroraLevel hourLevel[AURORA_HOURS] = {}; ///< Level of each hourly sample.
  uint8_t count = 0;                        ///< Valid samples.
};

namespace Aurora {

/**
 * @brief  Download the current status and the 24-hour activity series.
 * @param[out] out  Overwritten only on success.
 * @return true on success.
 */
bool fetch(AuroraData &out);

/** @brief "Green", "Yellow", "Amber", "Red" or "?". */
const char *levelName(AuroraLevel level);

/** @brief Short plain-English meaning for northern England. */
const char *meaning(AuroraLevel level);

/** @brief Most recent hourly activity value in nT (0 if none). */
float latestActivity(const AuroraData &a);

}  // namespace Aurora
