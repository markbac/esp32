/**
 * @file    settings.h
 * @brief   Runtime settings, editable from the web page and stored in NVS.
 *
 * @details Defaults come from config.h / secrets.h at build time; anything
 *          saved from http://<host>.local/settings overrides them and survives
 *          reboots and re-flashing (NVS namespace "wxcfg").
 *
 *          **Secrets are write-only:** the Home Assistant token, Wi-Fi password
 *          and admin password can be set or cleared from the web page but are
 *          never sent back out — the page and API only report whether each is
 *          set. If an admin password is set, saving requires it.
 *
 *          Thread-safe: get() returns a copy under a mutex.
 */
#pragma once

#include <Arduino.h>

/** @brief All runtime-editable settings. */
struct SettingsData {
  // Network (applied after restart).
  String wifiSsid;         ///< Empty = use the compiled-in SSID.
  String wifiPassword;     ///< Secret.
  // Home Assistant.
  String haUrl;            ///< e.g. http://10.0.10.244:8123
  String haToken;          ///< Secret: long-lived access token.
  // Home lab.
  String nasHost;          ///< Glances / Speedtest / Uptime Kuma host.
  uint16_t glancesPort = 0;
  uint16_t speedtestPort = 0;
  uint16_t kumaPort = 0;
  String kumaSlug;
  String sonosHosts;       ///< Comma-separated IPs.
  // Display (CrowPanel).
  uint8_t backlightDay = 0;
  uint8_t backlightNight = 0;  ///< 0 = no night dimming.
  bool autoPage = false;       ///< Move to the next page on a timer (CrowPanel).
  uint16_t autoPageSecs = 0;   ///< Seconds to stay on each page when autoPage is on.
  uint32_t autoPageMask = 0;   ///< Bit per page (board's Page enum) included in auto paging.
  bool sleepEnabled = false;   ///< Turn the screen off between sleepStartMin and sleepEndMin.
  uint16_t sleepStartMin = 0;  ///< Sleep from, minutes after local midnight (e.g. 22:00 = 1320).
  uint16_t sleepEndMin = 0;    ///< Wake at, minutes after local midnight (may be earlier than start).
  // Flights (both boards).
  uint16_t flightRadiusNm = 0; ///< Search radius around home, nautical miles.
  // Web settings protection.
  String adminPassword;    ///< Secret: required to save when set.
};

namespace Settings {

/** @brief Load defaults, then NVS overrides. Call once early in setup(). */
void begin();

/** @brief Copy of the current settings (includes secrets: do not send them anywhere). */
SettingsData get();

/** @brief Persist and apply new settings. */
bool save(const SettingsData &s);

/**
 * @brief  Apply new settings in memory now and write them to flash later.
 * @details Used by the touch Settings page so that a run of taps costs one
 *          flash write; call flush() regularly to persist.
 */
void set(const SettingsData &s);

/** @brief Write pending set() changes once @p quietMs have passed since the last one. */
void flush(uint32_t quietMs = 3000);

/** @brief True if @p minuteOfDay falls inside the sleep window (and sleep is enabled). */
bool inSleepWindow(const SettingsData &s, int minuteOfDay);

/** @brief Allowed flight radii (nm), smallest first. */
extern const uint16_t FLIGHT_RADII[];
extern const uint8_t FLIGHT_RADII_COUNT;

/** @brief Allowed auto-page dwell times (s), smallest first. */
extern const uint16_t PAGE_SECS[];
extern const uint8_t PAGE_SECS_COUNT;

/** @brief Next (@p dir > 0) or previous value of @p v in @p list, clamped to the ends. */
uint16_t step(const uint16_t *list, uint8_t n, uint16_t v, int dir);

/** @brief True if @p attempt matches the admin password, or none is set. */
bool checkAdmin(const String &attempt);

/** @brief Split the Sonos host list into at most @p max entries. */
uint8_t sonosList(String *out, uint8_t max);

}  // namespace Settings
