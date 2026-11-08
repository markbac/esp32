/**
 * @file    radar.h
 * @brief   Animated rain radar: RainViewer frames over a dark-styled OpenStreetMap base map.
 *
 * @details A @ref RadarLoop::W x @ref RadarLoop::H viewport centred on the
 *          configured location is composed from the 256 px tiles overlapping
 *          it at @ref RADAR_ZOOM. The base map is downloaded once and cached.
 *
 *          The loop is a ring of RADAR_FRAMES frame buffers plus one spare.
 *          New frames are composed into the spare without any lock, then
 *          inserted by moving pointers (the oldest buffer becomes the new
 *          spare), so memory is (RADAR_FRAMES + 2) frames regardless of how
 *          often it refreshes, and only frames newer than the newest held are
 *          ever downloaded.
 */
#pragma once

#include <Arduino.h>
#include <time.h>

#include "config.h"

/** @brief One composed radar frame. */
struct RadarFrame {
  time_t time = 0;             ///< Observation time (UTC epoch).
  uint16_t *pixels = nullptr;  ///< W*H RGB565 pixels (little-endian) in PSRAM.
};

/** @brief The animation: frames oldest first. */
struct RadarLoop {
  static constexpr int W = RADAR_VIEW_W;  ///< Viewport width, pixels.
  static constexpr int H = RADAR_VIEW_H;  ///< Viewport height, pixels.

  bool valid = false;               ///< True once at least one frame is composed.
  uint32_t fetchedAtMs = 0;         ///< millis() of the last successful refresh.
  uint8_t count = 0;                ///< Valid frames.
  RadarFrame frames[RADAR_FRAMES];  ///< Oldest first; frames[count-1] is newest.
};

/** @brief A frame listed in RainViewer's index. */
struct RadarFrameRef {
  time_t time = 0;  ///< Observation time.
  String path;      ///< URL prefix "<host><path>".
};

namespace Radar {

/** @brief Allocate one W*H frame buffer (PSRAM if available); nullptr on failure. */
uint16_t *allocFrame();

/**
 * @brief  Read the newest RADAR_FRAMES frames from RainViewer's index.
 * @param[out] refs  Oldest first.
 * @param[out] n     Number filled.
 * @return true on success.
 */
bool listFrames(RadarFrameRef (&refs)[RADAR_FRAMES], uint8_t &n);

/**
 * @brief  Compose one frame (cached base map + radar tiles) into @p dst.
 * @param  ref  Frame to download.
 * @param  dst  W*H buffer.
 * @return true on success.
 */
bool compose(const RadarFrameRef &ref, uint16_t *dst);

}  // namespace Radar
