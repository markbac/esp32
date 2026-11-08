/**
 * @file    status_led.h
 * @brief   On-board NeoPixel used as an at-a-glance alert light.
 *
 * @details Priority (highest first):
 *          - Met Office red / amber warning: solid red / amber
 *          - AuroraWatch red / amber: slow green "breathing" pulse
 *          - otherwise: off
 *          Brightness is kept low (LED_MAX_BRIGHTNESS) so it is not distracting.
 */
#pragma once

#include "aurora.h"
#include "warnings.h"

namespace StatusLed {

/** @brief Power up the NeoPixel and switch it off. */
void begin();

/**
 * @brief Update the LED colour; call frequently (~every 50 ms) for smooth pulsing.
 * @param warning  Highest active Met Office warning level.
 * @param aurora   Current AuroraWatch level.
 */
void update(WarningLevel warning, AuroraLevel aurora);

}  // namespace StatusLed
