/**
 * @file    wifi_scan.h
 * @brief   Background Wi-Fi survey: networks in range and their signal history.
 *
 * @details Every WIFI_SCAN_INTERVAL_MS an asynchronous scan is started while
 *          the station stays connected (the radio briefly visits each channel,
 *          about 2 s in all). Results are keyed by BSSID so two access points
 *          with the same SSID are tracked separately. Each network keeps one
 *          RSSI sample per scan for the last WIFI_HISTORY scans (3 hours at the
 *          default rate); a scan that misses a network records "not seen".
 *
 *          poll() is called from the UI loop and never blocks. copy() hands
 *          out a consistent snapshot under the module's own lock.
 */
#pragma once

#include <Arduino.h>

class WebServer;

/** @brief Networks tracked at once (the longest-unseen is replaced when full). */
constexpr uint8_t WIFI_MAX_NETS = 32;
/** @brief RSSI samples kept per network (one per scan). */
constexpr uint8_t WIFI_HISTORY = 180;
/** @brief Time between scans. */
constexpr uint32_t WIFI_SCAN_INTERVAL_MS = 60UL * 1000UL;
/** @brief History value meaning "not seen in that scan". */
constexpr int8_t WIFI_NOT_SEEN = -128;

/** @brief One access point. */
struct WifiNet {
  char ssid[33] = "";             ///< Network name ("" = hidden).
  uint8_t bssid[6] = {};          ///< Access point MAC.
  uint8_t channel = 0;
  uint8_t auth = 0;               ///< wifi_auth_mode_t (0 = open).
  int8_t rssi = WIFI_NOT_SEEN;    ///< Latest scan (WIFI_NOT_SEEN if missed).
  int8_t best = WIFI_NOT_SEEN;    ///< Strongest seen in the history window.
  uint32_t lastSeenScan = 0;      ///< Scan number it was last seen in.
  bool connected = false;         ///< The access point this device is joined to.
  int8_t hist[WIFI_HISTORY];      ///< Ring buffer, indexed like WifiScanData::head.
};

/** @brief Snapshot of the survey. */
struct WifiScanData {
  WifiNet nets[WIFI_MAX_NETS];
  uint8_t count = 0;              ///< Valid entries in @ref nets.
  uint32_t scans = 0;             ///< Completed scans since boot.
  uint8_t head = 0;               ///< Ring slot of the newest sample.
  uint8_t samples = 0;            ///< Valid samples in each ring (<= WIFI_HISTORY).
  uint32_t lastScanMs = 0;        ///< millis() when the last scan finished (0 = none yet).
  uint8_t lastFound = 0;          ///< Access points in the last scan.

  /** @brief RSSI @p back scans ago (0 = newest) for network @p i. */
  int8_t at(uint8_t i, uint8_t back) const {
    return nets[i].hist[(head + WIFI_HISTORY - back) % WIFI_HISTORY];
  }
};

/** @brief Most "own system" prefixes tracked (one per mesh node model / vendor block). */
constexpr uint8_t WIFI_MAX_FAMILY = 8;

/** @brief True if @p mac belongs to one of @p n family prefixes. */
inline bool inFamily(const uint8_t *mac, const uint8_t (*fam)[2], uint8_t n) {
  for (uint8_t i = 0; i < n; ++i) {
    if (mac[1] == fam[i][0] && mac[2] == fam[i][1]) return true;
  }
  return false;
}

namespace WifiScan {

/** @brief Start or collect scans as they fall due (call often from the UI loop). */
void poll();

/** @brief True while a scan is in progress (the radio is hopping channels). */
bool scanning();

/**
 * @brief Run one scan now, blocking ~2 s, if none has completed yet (so the
 *        network tools know your mesh nodes). Returns true if data exists after.
 */
bool ensureScanned();

/** @brief Copy the current survey into @p out (thread-safe). */
void copy(WifiScanData &out);

/**
 * @brief  Address prefixes of your own Wi-Fi system (router + mesh nodes, all their SSIDs).
 * @details Every access point broadcasting the SSID this panel is joined to is
 *          yours; mesh nodes and their guest/IoT/backhaul networks share bytes
 *          1-2 of the address with it (byte 0 varies per SSID), so those two
 *          bytes identify the family.
 * @param[out] out  Up to WIFI_MAX_FAMILY two-byte prefixes.
 * @return Number written (0 when not connected or no scan yet).
 */
uint8_t ownFamily(uint8_t (*out)[2]);

/** @brief Register GET /api/wifi.json: every network with signal now, best, channel, security and history. */
void registerRoutes(WebServer &server);

/** @brief Human-readable security name for a wifi_auth_mode_t. */
const char *authName(uint8_t auth);

}  // namespace WifiScan
