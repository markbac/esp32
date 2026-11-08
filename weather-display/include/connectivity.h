/**
 * @file    connectivity.h
 * @brief   Wi-Fi station management and NTP clock helpers.
 */
#pragma once

#include <Arduino.h>
#include <time.h>

namespace Net {

/**
 * @brief Start Wi-Fi in station mode and wait (bounded) for a connection.
 * @param ssid       Network name.
 * @param password   Network password.
 * @param timeoutMs  Maximum time to block; auto-reconnect continues afterwards.
 * @return true if connected before the timeout.
 */
bool begin(const char *ssid, const char *password, uint32_t timeoutMs);

/** @brief True while associated with the access point and holding an IP. */
bool connected();

/** @brief Received signal strength in dBm (0 when disconnected). */
int rssi();

/** @brief Signal quality 0-100 % derived linearly from RSSI (-100..-50 dBm). */
int quality();

/** @brief Signal strength as 0-4 bars for the status-bar icon. */
int bars();

/** @brief Connected SSID (empty when disconnected). */
String ssid();

/** @brief Dotted-quad IP address (empty when disconnected). */
String ip();

/**
 * @brief Configure the time zone and start SNTP synchronisation.
 * @param posixTz POSIX TZ rule, e.g. "GMT0BST,M3.5.0/1,M10.5.0".
 */
void beginTime(const char *posixTz);

/** @brief True once SNTP has set a plausible wall-clock time. */
bool timeValid();

/**
 * @brief Non-blocking local-time read.
 * @param[out] out Broken-down local time.
 * @return true if the clock is valid and @p out was filled.
 */
bool localTime(tm &out);

}  // namespace Net
