/**
 * @file    air_survey.h
 * @brief   Passive survey of the Wi-Fi channel: devices, airtime by category, retries, busiest devices.
 *
 * @details Uses the radio's promiscuous (listen) mode while staying connected,
 *          so it hears everything on the channel the panel is on - every one of
 *          your mesh units on that channel plus neighbours - but only 2.4 GHz,
 *          and devices far from the panel may be missed. For every frame it
 *          reads only the header: type, addresses, size, rate, signal and the
 *          retry flag. Payloads are encrypted and never looked at.
 *
 *          "Yours" is the whole system: every access point whose address shares
 *          bytes 1-2 with one broadcasting your SSID (WifiScan::ownFamily), so
 *          all mesh units and their guest/IoT/backhaul SSIDs count. Units are
 *          told apart with radioKey().
 *
 *          Air time per frame = preamble + size / rate. It is split into:
 *          your devices' data, your broadcast/multicast data, mesh backhaul
 *          (unit to unit), your beacons, your other management frames, neighbours' beacons, and everything
 *          else (neighbours' traffic, devices probing). The total is a lower
 *          bound: frames the radio cannot decode, and ACK/RTS/CTS control
 *          frames, are not counted.
 *
 *          A survey runs either timed (Crowding page / POST /api/nettest
 *          action=air: startTimed()) or open-ended for a monitor run
 *          (start() / take() each minute / stop()). The full device list of
 *          the last timed survey is served at GET /api/air.json.
 */
#pragma once

#include <Arduino.h>

class WebServer;

/** @brief One device's traffic in a survey. */
struct AirTalker {
  uint8_t mac[6] = {};
  uint32_t frames = 0;
  uint32_t bytes = 0;
  uint32_t airtimeUs = 0;   ///< Air time of frames to and from this device.
  int32_t rssiSum = 0;      ///< Sum of the signal of frames it sent (for the average).
  uint16_t rssiN = 0;
  uint16_t maxRate10 = 0;   ///< Fastest rate it sent at, Mbps x 10.
  uint8_t unit = 0xFF;      ///< Index into AirStats::units it last talked through (0xFF = unknown).
  bool backhaul = false;    ///< One of your mesh units relaying for others.

  int rssi() const { return rssiN ? static_cast<int>(rssiSum / rssiN) : 0; }
  /** @brief Locally administered address: a randomised "private" MAC (phones), no vendor. */
  bool randomised() const { return mac[0] & 0x02; }
};

/** @brief One of your mesh units heard on the channel. */
struct AirUnit {
  uint8_t mac[6] = {};      ///< One of its addresses (first heard).
  int8_t rssi = 0;          ///< Signal of its beacons here.
  uint16_t clients = 0;     ///< Devices that talked through it.
  uint32_t airtimeUs = 0;   ///< Air time of frames to/from it, excluding its beacons.
};

/** @brief A device whose broadcasts/multicasts your access points repeat to everyone. */
struct AirSource {
  uint8_t mac[6] = {};
  uint32_t frames = 0;
  uint32_t airtimeUs = 0;
};

/** @brief Kinds of broadcast/multicast, by destination address. */
enum class GroupKind : uint8_t { Broadcast, Mdns, Ssdp, Ipv6, OtherMulticast, Count };

constexpr uint8_t AIR_MAX_UNITS = 8;
constexpr uint8_t AIR_TOP_SOURCES = 5;
constexpr uint8_t AIR_TOP = 5;          ///< Busiest devices kept in AirStats.
constexpr uint8_t AIR_MAX_TALKERS = 64; ///< Devices tracked per survey.

/** @brief Results of a survey period. */
struct AirStats {
  uint32_t durationMs = 0;
  uint8_t channel = 0;
  uint32_t frames = 0;         ///< Management + data frames heard.
  uint32_t retries = 0;
  uint32_t airtimeUs = 0;      ///< Estimated air time of everything heard.
  // Air time by category (sums to airtimeUs).
  uint32_t ourDataUs = 0;      ///< Data between your units and your devices.
  uint32_t backhaulUs = 0;     ///< Data between your mesh units.
  uint32_t ourBeaconUs = 0;    ///< Your units' beacons.
  uint32_t ourGroupUs = 0;     ///< Broadcast/multicast data your units send to everyone (mDNS, SSDP, ARP...),
                               ///< usually at a slow basic rate - a classic 2.4 GHz air hog.
  uint32_t ourMgmtUs = 0;      ///< Your other management frames (probe responses, association, action frames).
  uint32_t otherBeaconUs = 0;  ///< Neighbours' beacons.
  uint32_t otherUs = 0;        ///< Everything else: neighbours' traffic, devices probing for networks.
  uint16_t devicesOurs = 0;    ///< Distinct devices exchanging data with your units (not the units).
  uint16_t devicesOther = 0;   ///< Distinct other transmitters on the channel.
  uint8_t accessPoints = 0;    ///< Distinct access-point addresses heard (beacons).
  uint8_t ourAccessPoints = 0; ///< Of which yours (SSIDs x units).
  uint32_t beacons = 0;
  AirUnit units[AIR_MAX_UNITS];
  uint8_t unitCount = 0;
  uint32_t groupKindUs[static_cast<int>(GroupKind::Count)] = {};  ///< ourGroupUs split by kind.
  AirSource sources[AIR_TOP_SOURCES];  ///< Devices whose broadcasts use the most air.
  uint8_t sourceCount = 0;
  AirTalker top[AIR_TOP];      ///< Busiest of your devices by air time.
  uint8_t topCount = 0;
  uint16_t talkers = 0;        ///< Devices in the full list.

  float pct(uint32_t us) const { return durationMs ? min(100.0f, us / (durationMs * 10.0f)) : 0; }
  float busyPct() const { return pct(airtimeUs); }
  float beaconPct() const { return pct(ourBeaconUs + otherBeaconUs); }
  float ourBeaconPct() const { return pct(ourBeaconUs); }
  float retryPct() const { return frames ? 100.0f * retries / frames : 0; }
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

/** @brief Copy the last timed survey's devices, busiest (air time) first; returns the count. */
uint8_t lastTalkers(AirTalker *out, uint8_t max);

/** @brief True while listening. */
bool active();

/** @brief Timed survey progress 0-100 (0 when none running). */
uint8_t progress();

/** @brief Short name for a broadcast kind ("broadcast/ARP", "mDNS", ...). */
const char *groupKindName(GroupKind k);

/** @brief Format a MAC as "aa:bb:cc:dd:ee:ff" into @p buf (>= 18 bytes). */
void macStr(const uint8_t *mac, char *buf);

/** @brief Register GET /api/air.json (last timed survey: summary, categories, units, every device). */
void registerRoutes(WebServer &server);

}  // namespace AirSurvey
