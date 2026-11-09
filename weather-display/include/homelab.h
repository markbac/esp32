/**
 * @file    homelab.h
 * @brief   Home-lab status from LAN services that need no login.
 *
 * @details Sources (hosts and ports in config.h):
 *          | Service                  | Endpoint                                        | Data                                |
 *          |--------------------------|-------------------------------------------------|-------------------------------------|
 *          | Glances (NAS)            | `/api/4/quicklook, fs, sensors, uptime, system, containers` | CPU, memory, disk, temperature, uptime, container health |
 *          | Speedtest Tracker        | `/api/speedtest/latest`                         | Last download/upload/ping           |
 *          | Uptime Kuma status page  | `/api/status-page/{slug}` + `/heartbeat/{slug}` | Monitors up/down                    |
 *          | Sonos speakers           | UPnP SOAP on :1400                              | Room, play state, track             |
 *          Each source is optional: a failure marks only that section stale.
 */
#pragma once

#include <Arduino.h>

/** @brief NAS health from Glances. */
struct NasStatus {
  bool ok = false;
  char host[24] = "";
  char cpuName[32] = "";
  char uptime[24] = "";
  float cpu = 0, mem = 0, swap = 0, disk = 0, diskTb = 0, temp = NAN, load = 0;
  uint8_t containers = 0, healthy = 0, unhealthy = 0;
  char unhealthyNames[96] = "";  ///< Comma-separated.
};

/** @brief Last internet speed test. */
struct SpeedStatus {
  bool ok = false;
  bool failed = false;              ///< The latest test itself failed (no figures).
  float down = 0, up = 0, ping = 0;  ///< Mbps, Mbps, ms.
  uint32_t testedAt = 0;             ///< UTC epoch.
  char server[32] = "";
};

/** @brief Uptime Kuma public status page summary. */
struct ServicesStatus {
  bool ok = false;
  uint8_t total = 0, up = 0, down = 0;
  char downNames[160] = "";  ///< Comma-separated.
};

/** @brief One Sonos speaker. */
struct SonosStatus {
  bool ok = false;
  char room[20] = "";
  char model[20] = "";
  char state[16] = "";   ///< PLAYING, PAUSED_PLAYBACK, STOPPED, TRANSITIONING.
  char title[48] = "";
  char artist[40] = "";
};

/** @brief Maximum Sonos speakers polled. */
constexpr uint8_t MAX_SONOS = 4;

/** @brief Everything on the Homelab page. */
struct HomelabData {
  bool valid = false;        ///< At least one source answered.
  uint32_t fetchedAtMs = 0;
  NasStatus nas;
  SpeedStatus speed;
  ServicesStatus services;
  SonosStatus sonos[MAX_SONOS];
  uint8_t sonosCount = 0;
};

namespace Homelab {

/** @brief Poll every configured source. */
bool fetch(HomelabData &out);

}  // namespace Homelab
