/**
 * @file    https_get.cpp
 * @brief   Implementation of the shared HTTPS GET helper.
 */
#include "https_get.h"

#include <WiFiClientSecure.h>

#include "connectivity.h"

namespace {
constexpr uint16_t HTTP_TIMEOUT_MS = 10000;  ///< Connect + read timeout.
}  // namespace

namespace Https {

bool get(const String &url, const char *tag, const BodyHandler &handler, bool http10, const char *userAgent) {
  if (!Net::connected()) return false;

  WiFiClientSecure client;
  client.setInsecure();  // Public data; avoids bundling and rotating CA certificates.
  HTTPClient http;
  http.useHTTP10(http10);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setUserAgent(userAgent ? userAgent : "ESP32-WeatherDisplay/1.0");

  if (!http.begin(client, url)) {
    Serial.printf("[%s] bad URL\n", tag);
    return false;
  }
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("[%s] HTTP error %d\n", tag, status);
    http.end();
    return false;
  }
  const bool ok = handler(http);
  http.end();
  return ok;
}

}  // namespace Https
