/**
 * @file    board_config.h
 * @brief   CrowPanel-specific settings (display timing, touch orientation, UI timing).
 */
#pragma once

#include <stdint.h>

/** @brief RGB pixel clock. 12-16 MHz works; lower is steadier when PSRAM is busy. */
#ifndef CP_PCLK_HZ
#define CP_PCLK_HZ 13000000UL
#endif

/** @brief Backlight level at boot (0-255). */
#ifndef CP_BACKLIGHT
#define CP_BACKLIGHT 200
#endif

/** @brief Night backlight level (between sunset+1h and sunrise), 0 = no dimming. */
#ifndef CP_BACKLIGHT_NIGHT
#define CP_BACKLIGHT_NIGHT 60
#endif

/** @brief Touch rotation passed to LovyanGFX (0-3). */
#ifndef CP_TOUCH_ROTATION
#define CP_TOUCH_ROTATION 0
#endif

/** @brief Set to 1 if touches register in the opposite corner (some panels are mounted 180 deg). */
#ifndef CP_TOUCH_FLIP
#define CP_TOUCH_FLIP 0
#endif

/** @brief UI timing. */
constexpr uint32_t CP_RENDER_MS = 1000;         ///< Full-frame redraw period (radar animates faster).
constexpr uint32_t CP_TOUCH_POLL_MS = 20;       ///< Touch sampling period.
constexpr int CP_SWIPE_MIN_PX = 90;             ///< Horizontal drag that counts as a swipe.
constexpr int CP_TAP_MAX_MOVE_PX = 25;          ///< Movement still counted as a tap.
