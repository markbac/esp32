/**
 * @file    connectivity.cpp
 * @brief   Implementation of Wi-Fi and clock helpers.
 */
#include "connectivity.h"
#include "log.h"

#include <WiFi.h>

#include "config.h"

namespace {
/** @brief Any epoch after Nov 2023 means SNTP has synchronised. */
constexpr time_t MIN_VALID_EPOCH = 1700000000;
constexpr uint32_t REJOIN_AFTER_MS = 20UL * 1000UL;  ///< Offline this long before forcing a re-join.
String savedSsid, savedPass;                         ///< From begin(), for maintain().
uint32_t downSinceMs = 0;                            ///< 0 = connected.
}  // namespace

namespace Net {

bool begin(const char *ssid, const char *password, uint32_t timeoutMs) {
  savedSsid = ssid;
  savedPass = password;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  Log::printf("[wifi] connecting to '%s'", ssid);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (connected()) {
    Log::printf("[wifi] connected, IP %s, RSSI %d dBm\n", ip().c_str(), rssi());
  } else {
    Log::printf("[wifi] not connected yet (status %d), retrying in background\n", WiFi.status());
  }
  return connected();
}

void maintain() {
  if (connected()) {
    downSinceMs = 0;
    return;
  }
  const uint32_t now = millis();
  if (!downSinceMs) {
    downSinceMs = now;
    return;
  }
  if (now - downSinceMs < REJOIN_AFTER_MS || savedSsid.isEmpty()) return;
  Log::printf("[wifi] offline %lus - re-joining '%s'\n", static_cast<unsigned long>((now - downSinceMs) / 1000),
              savedSsid.c_str());
  WiFi.disconnect();
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  downSinceMs = now;  // Give this attempt another REJOIN_AFTER_MS.
}

bool connected() { return WiFi.status() == WL_CONNECTED; }

int rssi() { return connected() ? WiFi.RSSI() : 0; }

int quality() { return connected() ? constrain(2 * (rssi() + 100), 0, 100) : 0; }

int bars() {
  if (!connected()) return 0;
  const int r = rssi();
  if (r >= -55) return 4;
  if (r >= -65) return 3;
  if (r >= -75) return 2;
  if (r >= -85) return 1;
  return 0;
}

String ssid() { return connected() ? WiFi.SSID() : String(); }

String ip() { return connected() ? WiFi.localIP().toString() : String(); }

void beginTime(const char *posixTz) { configTzTime(posixTz, NTP_SERVER_1, NTP_SERVER_2); }

bool timeValid() { return time(nullptr) > MIN_VALID_EPOCH; }

bool localTime(tm &out) { return timeValid() && getLocalTime(&out, 0); }

}  // namespace Net
