/**
 * @file    https_get.h
 * @brief   Shared HTTP(S) request helper used by every online and LAN data source.
 */
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

#include <functional>
#include <utility>
#include <vector>

namespace Https {

/**
 * @brief Callback that consumes a successful (HTTP 200) response.
 * @param http Open client; use getStream() to stream-parse or getString().
 * @return true if the body was parsed successfully.
 */
using BodyHandler = std::function<bool(HTTPClient &http)>;

/** @brief Extra request headers, e.g. {"Authorization", "Bearer ..."}. */
using Headers = std::vector<std::pair<const char *, String>>;

/** @brief Options for request(). */
struct Options {
  bool http10 = true;               ///< HTTP/1.0 (never chunked) vs 1.1 (read with getString()).
  const char *userAgent = nullptr;  ///< User-Agent override.
  Headers headers;                  ///< Extra headers.
  uint16_t timeoutMs = 10000;       ///< Connect + read timeout.
};

/**
 * @brief  Perform a GET or POST and hand a 200 response to @p handler.
 *
 * @details `https://` URLs use TLS without certificate validation (all
 *          sources are public data or on the LAN); `http://` URLs use a plain
 *          TCP client (LAN services such as Glances, Sonos, Home Assistant).
 *
 * @param  method   "GET" or "POST".
 * @param  url      Full http:// or https:// URL.
 * @param  tag      Short label for serial log messages.
 * @param  body     Request body (POST), may be empty.
 * @param  handler  Called only on HTTP 200.
 * @param  opt      Protocol options and headers.
 * @param[out] status  Optional: the HTTP status (or negative client error).
 * @return true if status was 200 and @p handler returned true.
 */
bool request(const char *method, const String &url, const char *tag, const String &body, const BodyHandler &handler,
             const Options &opt, int *status = nullptr);

/**
 * @brief  GET shorthand (kept for existing callers).
 * @param  http10     true: HTTP/1.0; false: HTTP/1.1 (servers answering 426 to 1.0).
 * @param  userAgent  Optional User-Agent override (some APIs require a contact URL).
 */
bool get(const String &url, const char *tag, const BodyHandler &handler, bool http10 = true,
         const char *userAgent = nullptr);

}  // namespace Https
