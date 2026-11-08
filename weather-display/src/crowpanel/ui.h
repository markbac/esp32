/**
 * @file    ui.h
 * @brief   CrowPanel 7" touch UI: pages, frame composition, touch handling.
 *
 * @details Every frame is drawn into a full-screen 800x480 sprite in PSRAM
 *          and pushed to the panel's frame buffer in one copy. A status bar
 *          (clock, date, page title, station/aurora/warning/Wi-Fi icons) sits
 *          at the top and a tab bar at the bottom; pages fill the 800x380 area
 *          between. Navigation: tap a tab, or swipe left/right anywhere.
 *
 *          Drawing reads the DataService store, so compose happens under the
 *          data lock; a separate canvas lock serialises the display loop and
 *          web-server snapshots.
 */
#pragma once

#include <stdint.h>

/** @brief The pages, in tab order. */
enum class Page : uint8_t {
  Now,       ///< Tempest live conditions, wind compass, outlook.
  Forecast,  ///< 24-hour graph and 7-day forecast.
  Radar,     ///< Animated radar map + 2-hour rain nowcast.
  Air,       ///< Air quality, pollutants, UV, pollen.
  Grid,      ///< Electricity carbon intensity, generation mix, 48 h forecast.
  Sky,       ///< Sun arc, day length, moon phase.
  Alerts,    ///< Met Office warnings, frost/ice risk, AuroraWatch, lightning.
  Flights,   ///< Tap-to-select aircraft scope with trails + photo and details.
  History,   ///< Today/yesterday/records summary and charts from the 90-day log.
  System,    ///< Network, Tempest hub/sensor, device and feed status.
  Count      ///< Number of pages (not a page).
};

namespace Ui {

/** @brief Web snapshot size (the screen scaled by 1/2). */
constexpr int WEB_W = 400;
constexpr int WEB_H = 240;

/** @brief Allocate the frame sprite (call after Display::begin()). */
bool begin();

/** @brief Compose the current page and push it to the panel. */
void render();

/** @brief Feed the latest touch state (call every CP_TOUCH_POLL_MS). */
void handleTouch(bool down, int x, int y);

/** @brief Render @p page off-screen and downscale it into @p out (WEB_W x WEB_H RGB565). */
void snapshot(uint8_t page, uint16_t *out);

/** @brief Page currently shown. */
Page current();

/** @brief Switch page. */
void show(Page p);

/** @brief Tab label for a page. */
const char *pageName(Page p);

/** @brief Diagnostics for the web API and System page. */
struct Stats {
  uint32_t composeMs = 0;   ///< Time to draw the last frame.
  uint32_t pushMs = 0;      ///< Time to copy it to the panel.
  int lastTapX = -1;        ///< Last tap position (-1 = none yet).
  int lastTapY = -1;
  uint32_t lastTapAtMs = 0; ///< millis() of the last tap.
};

/** @brief Latest render timings and touch position. */
const Stats &stats();

}  // namespace Ui
