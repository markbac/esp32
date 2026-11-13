/**
 * @file    net_test.h
 * @brief   On-demand Wi-Fi quality test: latency, jitter, packet loss, DNS and download speed.
 *
 * @details Runs in its own short-lived task so the screen stays responsive:
 *          1. 20 pings to the router (the Wi-Fi link itself),
 *          2. 20 pings to 1.1.1.1 (the internet connection),
 *          3. a DNS lookup,
 *          4. an HTTPS download from speed.cloudflare.com for up to 8 s.
 *
 *          While a monitor runs the radio also listens to the channel
 *          (AirSurvey) to count active devices, air time and retries, and a
 *          DNS lookup is timed each minute.
 *
 *          Lag and glitches show up as high or uneven ping times (jitter) and
 *          lost pings, so the router ping is the best test of a room's Wi-Fi.
 *          The download figure is what this ESP32 achieves (single-antenna
 *          2.4 GHz with TLS, typically 5-20 Mbps), not what a phone or PC
 *          would get: compare rooms with it rather than with your broadband
 *          speed.
 *
 *          **Monitor** mode is for problems that come and go: for 15-120
 *          minutes it pings the router and 1.1.1.1 once a second and keeps a
 *          per-minute log of signal, average and worst ping, lost pings,
 *          spikes (> MONITOR_SPIKE_MS) and Wi-Fi drop-outs. Router trouble
 *          means the Wi-Fi in that spot; internet-only trouble means the
 *          broadband (or something saturating it).
 */
#pragma once

#include <Arduino.h>

class WebServer;

/** @brief Round-trip times from one ping run. */
struct PingStats {
  uint16_t sent = 0;
  uint16_t received = 0;
  float minMs = 0, avgMs = 0, maxMs = 0;
  float jitterMs = 0;  ///< Mean difference between consecutive replies.
  /** @brief Lost pings, percent. */
  float lossPct() const { return sent ? 100.0f * (sent - received) / sent : 0; }
};

/** @brief Result of the latest test. */
struct NetTestResult {
  enum class State : uint8_t { Idle, Running, Done };
  State state = State::Idle;
  char stage[32] = "";          ///< What is running now (while Running).
  uint8_t progress = 0;         ///< 0-100 while Running.
  int rssi = 0;                 ///< Signal (dBm) when the test started.
  PingStats router;             ///< Pings to the default gateway.
  PingStats internet;           ///< Pings to 1.1.1.1.
  float dnsMs = -1;             ///< DNS lookup time (-1 = failed).
  float downMbps = -1;          ///< Download speed (-1 = failed).
  uint32_t downBytes = 0;
  uint32_t finishedAtMs = 0;    ///< millis() when it finished.

  /** @brief One-word verdict: "Good", "OK", "Poor" or "Bad". */
  const char *verdict() const;
};

/** @brief Ping time that counts as a lag spike. */
constexpr uint16_t MONITOR_SPIKE_MS = 200;
/** @brief Longest monitor run (one log entry per minute). */
constexpr uint16_t MONITOR_MAX_MIN = 480;
/** @brief Notable events kept per monitor run (oldest dropped when full). */
constexpr uint8_t MONITOR_MAX_EVENTS = 100;

/** @brief One minute of a monitor run. */
struct MonitorMinute {
  time_t at = 0;                         ///< Start of the minute (epoch).
  int8_t rssiAvg = 0, rssiMin = 0;       ///< Signal, dBm.
  uint16_t routerAvg = 0, routerMax = 0; ///< Router ping, ms.
  uint16_t inetAvg = 0, inetMax = 0;     ///< Internet ping, ms.
  uint8_t routerSent = 0, routerLost = 0;
  uint8_t inetSent = 0, inetLost = 0;
  uint8_t spikes = 0;                    ///< Pings (either target) over MONITOR_SPIKE_MS.
  uint8_t dropouts = 0;                  ///< Times Wi-Fi disconnected in this minute.
  uint8_t roams = 0;                     ///< Moves to another access point / channel.
  uint8_t devices = 0;                   ///< Devices exchanging data with your router (air survey).
  uint8_t otherDevices = 0;              ///< Other transmitters on the channel.
  uint8_t busyPct = 0;                   ///< Estimated channel air time in use.
  uint8_t retryPct = 0;                  ///< Frames that had to be re-sent.
  uint16_t dnsMs = 0;                    ///< DNS lookup time (0 = not checked, 0xFFFF = failed).
  uint8_t topMac[3][6] = {};             ///< Busiest devices this minute, by air time.
  uint8_t topPct[3] = {};                ///< Their share of the air, %.
};

/** @brief Kinds of monitor event. */
enum class MonitorEventType : uint8_t { Start, End, Drop, Back, Roam, Spike, Loss, DnsFail, Weak, Busy, Retries, Hog };

/** @brief Something notable during a monitor run, for the Events list and the log. */
struct MonitorEvent {
  time_t at = 0;
  MonitorEventType type = MonitorEventType::Start;
  int16_t a = 0, b = 0;  ///< Meaning depends on type (see NetTest::describe()).
  uint8_t mac[6] = {};   ///< Device involved (Hog).
};

/** @brief State and log of the current/last monitor run (about 17 KB; kept in PSRAM). */
struct MonitorData {
  bool running = false;
  uint16_t durationMin = 0;              ///< Requested length.
  uint32_t startedAtMs = 0, endedAtMs = 0;
  uint16_t count = 0;                    ///< Completed minutes in @ref log.
  MonitorMinute log[MONITOR_MAX_MIN];
  MonitorMinute current;                 ///< Minute in progress.
  MonitorEvent events[MONITOR_MAX_EVENTS];  ///< Oldest first.
  uint8_t eventCount = 0;
  // Totals over the run.
  uint32_t routerSent = 0, routerLost = 0, inetSent = 0, inetLost = 0;
  uint32_t spikes = 0, dropouts = 0, roams = 0, dnsFails = 0;
  uint16_t worstMs = 0;                  ///< Highest ping (either target).
  time_t worstAt = 0;
  int8_t rssiMin = 0;                    ///< Weakest signal seen.
  uint8_t devicesMax = 0;                ///< Most devices active on your router in one minute.
  uint8_t busyMax = 0;                   ///< Busiest minute, % air time.
  bool airSurvey = false;                ///< Device / air time figures were collected.
  /** @brief One-word verdict for the run so far. */
  const char *verdict() const;
};

namespace NetTest {

/** @brief Start a test unless one is running; returns false if busy or offline. */
bool start();

/** @brief Copy of the current/last result (thread-safe). */
NetTestResult get();

/** @brief True while a test runs. */
bool running();

/** @brief Start a monitor run of @p minutes (1-MONITOR_MAX_MIN); false if busy or offline. */
bool startMonitor(uint16_t minutes);

/** @brief One-line description of @p e ("Wi-Fi dropped: beacon timeout"). */
void describe(const MonitorEvent &e, char *buf, size_t n);

/** @brief Short name for a Wi-Fi disconnect reason code. */
const char *reasonName(int reason);

/** @brief Stop a monitor run early (its log is kept). */
void stopMonitor();

/** @brief True while monitoring. */
bool monitoring();

/**
 * @brief Register POST /api/nettest on @p server: action=test | air | monitor (with minutes=) | stop.
 *        Results appear in /api/now.json ("netTest", "monitor") and the network log.
 */
void registerRoutes(WebServer &server);

/** @brief Copy the monitor state into @p out (thread-safe; about 17 KB, so keep @p out off the stack). */
void monitor(MonitorData &out);

}  // namespace NetTest
