/**
 * @file    air_survey.h
 * @brief   Passive survey of the Wi-Fi channel: devices, airtime, retries, busiest devices.
 *
 * @details Uses the radio's promiscuous (listen) mode while staying connected,
 *          so it only hears the channel the panel is on (your router's) and
 *          only 2.4 GHz. For every frame it reads just the addresses, size,
 *          rate and retry flag from the header - payloads are encrypted and
 *          never looked at, and nothing is stored beyond counts.
 *
 *          From that it estimates:
 *          - **devices on your router**: distinct addresses exchanging data
 *            with your access point, counting every network the same router
 *            broadcasts (main, guest, IoT) - phones, consoles, TVs...;
 *          - **other devices on the channel**: transmitters not on your
 *            network (neighbours sharing the airtime);
 *          - **channel busy %**: sum of each frame's air time (size / rate +
 *            preamble) over the elapsed time. Frames the radio cannot decode
 *            are missed, so this is a lower bound;
 *          - **retry %**: frames sent again because the first try failed -
 *            high values mean interference or weak links;
 *          - **busiest devices** on your router by bytes;
 *          - **beacon overhead**: air time spent on access points announcing
 *            themselves. Each SSID on each mesh node beacons ~10 times a
 *            second at a slow rate, so many SSIDs x nodes on one channel can
 *            use a large share of the air before any data is sent.
 *
 *          "Your network" is the whole system: every access point whose
 *          address shares bytes 1-2 with one broadcasting your SSID
 *          (WifiScan::ownFamily), so guest/IoT SSIDs and mesh nodes count.
 *
 *          A survey runs either timed (the Crowding page, startTimed()) or
 *          open-ended for a monitor run (start() / take() each minute / stop()).
 */
#pragma once

#include <Arduino.h>

/** @brief One device's traffic in a survey. */
struct AirTalker {
  uint8_t mac[6] = {};
  uint32_t frames = 0;
  uint32_t bytes = 0;
};

/** @brief Results of a survey period. */
struct AirStats {
  uint32_t durationMs = 0;
  uint8_t channel = 0;
  uint32_t frames = 0;         ///< Management + data frames heard.
  uint32_t ourFrames = 0;      ///< Of which to/from your access point.
  uint32_t retries = 0;
  uint32_t airtimeUs = 0;      ///< Estimated air time of everything heard.
  uint32_t ourAirtimeUs = 0;   ///< Of which your network's.
  uint16_t devicesOurs = 0;    ///< Distinct clients of your access point.
  uint16_t devicesOther = 0;   ///< Distinct other transmitters on the channel.
  uint8_t accessPoints = 0;    ///< Distinct access points heard (beacons).
  AirTalker top[5];            ///< Busiest clients of your access point, by bytes.
  uint8_t topCount = 0;
  uint32_t ourBytes = 0;       ///< Bytes to/from your access point.
  uint32_t talkerBytes = 0;    ///< Data bytes of all your clients (for busiest-device shares).
  uint32_t beacons = 0;        ///< Beacon frames heard (every access point sends ~10 a second).
  uint32_t beaconAirtimeUs = 0;    ///< Air time spent on beacons.
  uint32_t ourBeaconAirtimeUs = 0; ///< Of which your own system's (router + mesh, all SSIDs).
  uint8_t ourAccessPoints = 0;     ///< Your system's access points heard (all SSIDs, all nodes).

  float busyPct() const { return durationMs ? min(100.0f, airtimeUs / (durationMs * 10.0f)) : 0; }
  float ourBusyPct() const { return durationMs ? min(100.0f, ourAirtimeUs / (durationMs * 10.0f)) : 0; }
  float retryPct() const { return frames ? 100.0f * retries / frames : 0; }
  float beaconPct() const { return durationMs ? min(100.0f, beaconAirtimeUs / (durationMs * 10.0f)) : 0; }
  float ourBeaconPct() const { return durationMs ? min(100.0f, ourBeaconAirtimeUs / (durationMs * 10.0f)) : 0; }
};

namespace AirSurvey {

/** @brief Start an open-ended survey (for a monitor run). False if offline. */
bool start();

/** @brief Start a survey that stops itself after @p ms (call poll()). False if busy or offline. */
bool startTimed(uint32_t ms);

/** @brief Stop listening. */
void stop();

/** @brief Finish a timed survey when due (call about once a second). */
void poll();

/** @brief Results since the last take() / start(), then reset the counters. */
AirStats take();

/** @brief Last finished timed survey (durationMs 0 = none yet). */
AirStats lastTimed();

/** @brief True while listening. */
bool active();

/** @brief Timed survey progress 0-100 (0 when none running). */
uint8_t progress();

}  // namespace AirSurvey
