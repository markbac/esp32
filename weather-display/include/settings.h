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

/** @brief True if @p attempt matches the admin password, or none is set. */
bool checkAdmin(const String &attempt);

/** @brief Split the Sonos host list into at most @p max entries. */
uint8_t sonosList(String *out, uint8_t max);

}  // namespace Settings
