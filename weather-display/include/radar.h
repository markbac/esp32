/**
 * @file    radar.h
 * @brief   Animated rain radar: RainViewer frames over a dark-styled OpenStreetMap base map.
 *
 * @details A @ref RadarLoop::W x @ref RadarLoop::H viewport centred on the
 *          configured location is composed from up to four 256 px tiles at
 *          @ref RADAR_ZOOM, for each of the last @ref RADAR_FRAMES radar
 *          frames (10 minutes apart). The base map is downloaded once and
 *          cached; on each refresh only frames not already held are downloaded,
 *          so normally just one new frame per 10 minutes.
 */
#pragma once

#include <Arduino.h>
#include <time.h>

#include "config.h"

/** @brief One composed radar frame. */
struct RadarFrame {
  time_t time = 0;             ///< Observation time (UTC epoch).
  uint16_t *pixels = nullptr;  ///< W*H RGB565 pixels in PSRAM, owned by the loop.
};

/** @brief The animation: frames oldest first. */
struct RadarLoop {
  static constexpr int W = 160;     ///< Viewport width, pixels.
  static constexpr int H = 110;     ///< Viewport height, pixels.

  bool valid = false;               ///< True once at least one frame is composed.
  uint32_t fetchedAtMs = 0;         ///< millis() of the last successful refresh.
  uint8_t count = 0;                ///< Valid frames.
  RadarFrame frames[RADAR_FRAMES];  ///< Oldest first; frames[count-1] is newest.
};

namespace Radar {

/**
 * @brief  Allocate every frame buffer of @p loop (PSRAM if available).
 * @return true on success.
 */
bool allocate(RadarLoop &loop);

/**
 * @brief  Build an up-to-date loop in @p staging.
 * @param  staging  Allocated loop to fill (overwritten).
 * @param  current  Loop currently on screen; frames it already has are copied
 *                  instead of downloaded. Only the worker task writes it, so it
 *                  may be read here without the data lock.
 * @return true on success.
 */
bool fetch(RadarLoop &staging, const RadarLoop &current);

}  // namespace Radar
