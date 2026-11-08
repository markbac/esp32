/**
 * @file    warnings.h
 * @brief   Met Office National Severe Weather Warnings (regional RSS feed).
 *
 * @details Feed: https://www.metoffice.gov.uk/public/data/PWSCache/WarningsRSS/Region/<code>
 *          Each RSS <item> is one warning for one region. Titles look like
 *          "Yellow warning of fog affecting North West England"; descriptions
 *          list the affected areas and end "valid from 0000 Sun 08 Nov to 1000 Sun 08 Nov".
 */
#pragma once

#include <Arduino.h>

/** @brief Met Office warning colour, in increasing severity. */
enum class WarningLevel : uint8_t { None, Yellow, Amber, Red };

/** @brief One parsed weather warning. */
struct WeatherWarning {
  WarningLevel level = WarningLevel::None;  ///< Severity colour.
  char hazard[24] = "";                     ///< e.g. "Rain", "Wind", "Snow, ice".
  char validity[48] = "";                   ///< e.g. "0000 Sun 08 Nov - 1000 Sun 08 Nov".
};

/** @brief Maximum warnings kept (the screen shows three). */
constexpr uint8_t MAX_WARNINGS = 4;

/** @brief Result of one feed download. */
struct WarningList {
  bool valid = false;                    ///< False until the first successful fetch.
  uint32_t fetchedAtMs = 0;              ///< millis() at fetch time.
  WeatherWarning items[MAX_WARNINGS];    ///< Sorted most severe first.
  uint8_t count = 0;                     ///< Entries stored in @ref items.
  uint8_t total = 0;                     ///< Matching warnings in the feed (may exceed count).

  /** @brief Highest severity present, or None. */
  WarningLevel highest() const { return count ? items[0].level : WarningLevel::None; }
};

namespace Warnings {

/**
 * @brief  Download and parse the regional warnings feed.
 * @param[out] out  Overwritten only on success (an empty list is a success).
 * @return true on success.
 */
bool fetch(WarningList &out);

/** @brief "Yellow", "Amber", "Red" or "". */
const char *levelName(WarningLevel level);

}  // namespace Warnings
