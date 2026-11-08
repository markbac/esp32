/**
 * @file    https_get.cpp
 * @brief   Implementation of the shared HTTP(S) request helper.
 */
#include "https_get.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "connectivity.h"

namespace Https {

bool request(const char *method, const String &url, const char *tag, const String &body, const BodyHandler &handler,
             const Options &opt, int *statusOut) {
  if (statusOut) *statusOut = 0;
  if (!Net::connected()) return false;

  const bool tls = url.startsWith("https://");
  WiFiClientSecure secure;
  WiFiClient plain;
  if (tls) secure.setInsecure();  // Public data; avoids bundling and rotating CA certificates.

  HTTPClient http;
  http.useHTTP10(opt.http10);
  http.setTimeout(opt.timeoutMs);
  http.setConnectTimeout(opt.timeoutMs);
  http.setUserAgent(opt.userAgent ? opt.userAgent : "ESP32-WeatherDisplay/1.0");

  const bool begun = tls ? http.begin(secure, url) : http.begin(plain, url);
  if (!begun) {
    Serial.printf("[%s] bad URL\n", tag);
    return false;
  }
  for (const auto &h : opt.headers) http.addHeader(h.first, h.second);

  const int status = strcmp(method, "POST") == 0 ? http.POST(body) : http.GET();
  if (statusOut) *statusOut = status;
  if (status != HTTP_CODE_OK) {
    Serial.printf("[%s] HTTP error %d\n", tag, status);
    http.end();
    return false;
  }
  const bool ok = handler(http);
  http.end();
  return ok;
}

bool get(const String &url, const char *tag, const BodyHandler &handler, bool http10, const char *userAgent) {
  Options opt;
  opt.http10 = http10;
  opt.userAgent = userAgent;
  return request("GET", url, tag, String(), handler, opt);
}

}  // namespace Https
