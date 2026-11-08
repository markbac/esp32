/**
 * @file    config.h
 * @brief   Compile-time configuration: location, units, timing and layout.
 *
 * @details Everything a user is likely to want to tweak lives here so the
 *          rest of the code base never needs editing for routine changes.
 */
#pragma once

#include <stdint.h>

/** @name Location
 *  Coordinates and time zone passed to the Open-Meteo API. */
///@{
#define WX_LATITUDE   "53.826679"         ///< Decimal degrees, north positive.
#define WX_LONGITUDE  "-2.452993"         ///< Decimal degrees, east positive.
#define WX_TIMEZONE   "Europe%2FLondon"   ///< URL-encoded IANA zone for API timestamps.
///@}

/** @brief POSIX TZ string used by the on-board clock (UK: GMT with BST). */
#define POSIX_TZ      "GMT0BST,M3.5.0/1,M10.5.0"

/** @brief NTP servers used to set the clock. */
#define NTP_SERVER_1  "pool.ntp.org"
#define NTP_SERVER_2  "time.google.com"

/** @name Met Office warnings
 *  Region codes: os (Orkney & Shetland), he (Highlands & Eilean Siar), gr (Grampian),
 *  st (Strathclyde), ta (Central, Tayside & Fife), dg (SW Scotland, Lothian Borders),
 *  ni (Northern Ireland), wl (Wales), nw (North West England), ne (North East England),
 *  yh (Yorkshire & Humber), wm (West Midlands), em (East Midlands), ee (East of England),
 *  sw (South West England), se (London & South East England). */
///@{
#ifndef WARNING_REGION
#define WARNING_REGION       "nw"          ///< Region feed to download (overridable with -D).
#endif
#ifndef WARNING_AREA_FILTER
#define WARNING_AREA_FILTER  "Lancashire"  ///< Only keep warnings naming this area ("" = whole region).
#endif
///@}

/** @name Rain radar */
///@{
constexpr int RADAR_ZOOM = 7;                  ///< Tile zoom (7 = ~0.7 km/px here; RainViewer's max).
#define RADAR_COLOR_SCHEME  "2"                ///< RainViewer palette id.
constexpr uint8_t RADAR_FRAMES = 12;           ///< Frames in the loop (10 min apart = 2 h).
constexpr uint16_t RADAR_FRAME_MS = 450;       ///< Animation step.
constexpr uint8_t RADAR_HOLD_STEPS = 4;        ///< Extra steps the newest frame is held.
///@}

/** @name Alert light (on-board NeoPixel) */
///@{
constexpr bool LED_ENABLED = true;             ///< Set false to keep the LED dark.
constexpr uint8_t LED_MAX_BRIGHTNESS = 40;     ///< 0-255.
///@}

/** @name History log and web server */
///@{
#define HOSTNAME "weather"                     ///< mDNS name: http://weather.local
constexpr uint32_t HISTORY_INTERVAL_S = 15 * 60;  ///< One record per quarter hour.
constexpr uint16_t HISTORY_CAPACITY = 8 * 96;     ///< 8 days of quarter-hours (~26 KB).
constexpr uint8_t HISTORY_DEFAULT_DAYS = 7;       ///< Web charts default range.
///@}

/** @name Flight tracking */
///@{
constexpr int FLIGHT_RADIUS_NM = 25;           ///< Search radius around home, nautical miles.
///@}

/** @name Timing (milliseconds) */
///@{
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20UL * 1000UL;       ///< Blocking connect at boot.
constexpr uint32_t WEATHER_INTERVAL_MS     = 10UL * 60UL * 1000UL; ///< Normal refresh period.
constexpr uint32_t WEATHER_RETRY_MS        = 60UL * 1000UL;        ///< Retry period after any failed fetch.
constexpr uint32_t AIR_INTERVAL_MS         = 30UL * 60UL * 1000UL; ///< Air quality refresh (hourly model).
constexpr uint32_t WARNINGS_INTERVAL_MS    = 15UL * 60UL * 1000UL; ///< Met Office warnings refresh.
constexpr uint32_t RADAR_INTERVAL_MS       = 10UL * 60UL * 1000UL; ///< New radar frames every 10 min.
constexpr uint32_t FLIGHTS_INTERVAL_MS     = 30UL * 1000UL;        ///< Aircraft positions.
constexpr uint32_t AURORA_INTERVAL_MS      = 5UL * 60UL * 1000UL;  ///< AuroraWatch (server caches 3 min).
constexpr uint32_t BATTERY_INTERVAL_MS     = 5UL * 1000UL;         ///< Fuel-gauge poll period.
constexpr uint32_t PAGE_ROTATE_MS          = 12UL * 1000UL;        ///< Auto page change (0 = off).
constexpr uint32_t RENDER_INTERVAL_MS      = 250UL;                ///< Screen redraw period.
constexpr uint32_t SERIAL_LOG_INTERVAL_MS  = 30UL * 1000UL;        ///< Diagnostic log period.
///@}

/** @name Forecast layout */
///@{
constexpr uint8_t HOURLY_COLUMNS = 6;   ///< Columns shown on the hourly page.
constexpr uint8_t HOURLY_STEP    = 2;   ///< Hours between hourly columns.
constexpr uint8_t DAILY_DAYS     = 5;   ///< Days shown on the daily page.
///@}

/** @brief Hourly samples requested/stored (enough for the columns plus margin). */
constexpr uint8_t HOURLY_MAX = 1 + HOURLY_COLUMNS * HOURLY_STEP;

/** @name Buttons (Reverse TFT Feather)
 *  D0 is the BOOT button (active low, internal pull-up).
 *  D1 and D2 are active high (pull-down). */
///@{
constexpr int BUTTON_NEXT_PIN    = 0;  ///< D0: next page.
constexpr int BUTTON_PREV_PIN    = 1;  ///< D1: previous page.
constexpr int BUTTON_REFRESH_PIN = 2;  ///< D2: fetch weather now.
///@}
