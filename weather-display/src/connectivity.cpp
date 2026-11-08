/**
 * @file    connectivity.cpp
 * @brief   Implementation of Wi-Fi and clock helpers.
 */
#include "connectivity.h"

#include <WiFi.h>

#include "config.h"

namespace {
/** @brief Any epoch after Nov 2023 means SNTP has synchronised. */
constexpr time_t MIN_VALID_EPOCH = 1700000000;
}  // namespace

namespace Net {

bool begin(const char *ssid, const char *password, uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  Serial.printf("[wifi] connecting to '%s'", ssid);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (connected()) {
    Serial.printf("[wifi] connected, IP %s, RSSI %d dBm\n", ip().c_str(), rssi());
  } else {
    Serial.printf("[wifi] not connected yet (status %d), retrying in background\n", WiFi.status());
  }
  return connected();
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
