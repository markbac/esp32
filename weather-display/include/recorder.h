/**
 * @file    recorder.h
 * @brief   Writes one history record per quarter hour from the live data.
 *
 * @details Shared by both boards. Call poll() regularly from the UI loop; it
 *          appends a record when a new quarter-hour slot starts (once the
 *          clock is valid and the first forecast has arrived). Board-specific
 *          values (e.g. the Feather's LiPo gauge) are passed in.
 */
#pragma once

#include <Arduino.h>

namespace Recorder {

/** @brief Optional device battery reading for the record. */
struct DeviceBattery {
  bool present = false;  ///< false = no battery (fields ignored).
  float percent = 0;     ///< State of charge, %.
  float voltage = 0;     ///< Cell voltage, V.
};

/**
 * @brief Restore state after boot: skip a slot already logged and seed the
 *        Tempest rain-today total from today's records.
 */
void begin();

/**
 * @brief  Append a record if a new quarter hour has started.
 * @param  battery  Device battery (Feather) or default (none).
 * @return true if a record was written.
 */
bool poll(const DeviceBattery &battery = DeviceBattery{});

}  // namespace Recorder
