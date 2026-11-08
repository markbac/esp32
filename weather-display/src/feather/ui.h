/**
 * @file    ui.h
 * @brief   Screen layout and page rendering for the 240 x 135 TFT.
 *
 * @details Every frame is drawn into an off-screen 16-bit canvas and pushed to
 *          the ST7789 in one transfer, which avoids flicker. A status bar
 *          (clock, date, aurora + warning icons, Wi-Fi bars, battery) is shown
 *          on every page. Page implementations live in src/ui/.
 *
 *          The same renderer also produces screenshots for the web server
 *          (@ref Ui::snapshot). All drawing must happen while holding the
 *          DataService lock, which also serialises access to the canvas.
 */
#pragma once

#include <stdint.h>

#include "air_quality.h"
#include "aurora.h"
#include "battery.h"
#include "flights.h"
#include "radar.h"
#include "warnings.h"
#include "weather.h"

/** @brief Pages the user can cycle through with the D0 / D1 buttons. */
enum class Page : uint8_t {
  Now,      ///< Current conditions.
  Hourly,   ///< Next hours, one column per @ref HOURLY_STEP hours.
  Daily,    ///< Multi-day forecast.
  Radar,    ///< Animated rain radar and 2-hour "rain soon" chart.
  Air,      ///< Air quality, UV and pollen.
  Alerts,   ///< Met Office warnings + AuroraWatch status (auto-rotate skips when quiet).
  Sky,      ///< Sunrise, sunset, day length and moon phase.
  Flights,  ///< Nearby aircraft on a radar scope plus nearest-flight details.
  System,   ///< Wi-Fi, battery and uptime diagnostics.
  Count     ///< Number of pages (not a page).
};

/** @brief Everything the renderer needs for one frame (read-only views). */
struct UiModel {
  const WeatherData &weather;     ///< Forecast (may be invalid before first fetch).
  const AirQualityData &air;      ///< Air quality (may be invalid).
  const WarningList &warnings;    ///< Met Office warnings (may be invalid).
  const AuroraData &aurora;       ///< AuroraWatch status (may be invalid).
  const RadarLoop &radar;         ///< Radar animation (may be invalid).
  const FlightList &flights;      ///< Nearby aircraft (may be invalid).
  const BatteryStatus &battery;   ///< Latest battery reading.
};

namespace Ui {

/** @brief Screen size in pixels (landscape). */
constexpr int WIDTH = 240;
constexpr int HEIGHT = 135;

/** @brief Power the display, initialise the ST7789 and clear the screen. */
void begin();

/**
 * @brief Draw @p page and push it to the display. Caller holds the data lock.
 * @param page   Page to show.
 * @param model  Data to display.
 */
void render(Page page, const UiModel &model);

/**
 * @brief Draw @p page off-screen and copy the pixels out (for the web server).
 *        Caller holds the data lock. The display is not touched.
 * @param page   Page to draw.
 * @param model  Data to display.
 * @param[out] out  WIDTH*HEIGHT RGB565 pixels, row-major.
 */
void snapshot(Page page, const UiModel &model, uint16_t *out);

/** @brief Human-readable page name, e.g. "Radar". */
const char *pageName(Page p);

/** @brief Page before @p p, wrapping around. */
Page prev(Page p);

/** @brief Page after @p p, wrapping around. */
Page next(Page p);

}  // namespace Ui
