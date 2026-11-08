/**
 * @file    ui_draw.h
 * @brief   Internal to the UI: shared canvas, layout constants, text helpers
 *          and the per-page render functions.
 *
 * @details Screen map (landscape, 240 x 135):
 * @verbatim
 *   y 0..17   status bar: HH:MM  Day DD  [aurora] [!] [wifi] 87% [batt]
 *   y 19..127 page body
 *   y 131     page indicator dots
 * @endverbatim
 *          Text uses the built-in 6 x 8 GFX font scaled 1-5x.
 */
#pragma once

#include <Adafruit_GFX.h>

#include "ui.h"

namespace UiDraw {

constexpr int W = 240;       ///< Screen width in landscape.
constexpr int H = 135;       ///< Screen height in landscape.
constexpr int BAR_H = 18;    ///< Status bar height.
constexpr int CHAR_W = 6;    ///< Built-in font cell width at size 1.

/** @brief Off-screen frame buffer every page draws into. */
extern GFXcanvas16 canvas;

/** @brief Abbreviated weekday names indexed 0 = Sunday. */
extern const char *const WEEKDAYS[7];

/** @brief Print text at (x, y). */
void text(const char *s, int x, int y, uint8_t size, uint16_t color);

/** @brief Print text horizontally centred on @p cx. */
void centered(const char *s, int cx, int y, uint8_t size, uint16_t color);

/** @brief Print text right-aligned so it ends at @p right. */
void rightAligned(const char *s, int right, int y, uint8_t size, uint16_t color);

/** @brief Small ring used as a degree symbol; @p r is its radius. */
void degree(int x, int y, int r, uint16_t color);

/** @brief Temperature (rounded) plus degree ring, centred on @p cx. */
void tempCentered(float t, int cx, int y, uint8_t size, uint16_t color);

/** @brief Centred placeholder used before data arrives. */
void waiting(const char *what);

/** @brief RGB565 colour for a Met Office warning level. */
uint16_t warningColor(WarningLevel level);

/** @brief Filled warning triangle with "!" (13 x 12). */
void warningIcon(int x, int y, WarningLevel level);

/** @brief RGB565 colour for an AuroraWatch level. */
uint16_t auroraColor(AuroraLevel level);

/** @brief Aurora "curtain" icon (13 x 12) in the level's colour. */
void auroraIcon(int x, int y, AuroraLevel level);

/** @brief Format a UTC epoch as local "HH:MM" into @p buf (6 bytes). */
void localHHMM(time_t t, char *buf);

/** @name Page renderers (one per Page value) */
///@{
void pageNow(const UiModel &m);
void pageHourly(const UiModel &m);
void pageDaily(const UiModel &m);
void pageRadar(const UiModel &m);
void pageAir(const UiModel &m);
void pageAlerts(const UiModel &m);
void pageSky(const UiModel &m);
void pageFlights(const UiModel &m);
void pageSystem(const UiModel &m);
///@}

}  // namespace UiDraw
