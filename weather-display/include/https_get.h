/**
 * @file    https_get.h
 * @brief   Shared HTTPS GET helper used by all online data sources.
 */
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

#include <functional>

namespace Https {

/**
 * @brief Callback that consumes a successful (HTTP 200) response.
 * @param http Open client; use getStream() to stream-parse or getString().
 * @return true if the body was parsed successfully.
 */
using BodyHandler = std::function<bool(HTTPClient &http)>;

/**
 * @brief  Perform an HTTPS GET and hand the response to @p handler.
 *
 * @details Uses HTTP/1.0 so the body is never chunked (simpler streaming), a
 *          10 s timeout, and no certificate validation: all sources are public,
 *          non-sensitive data.
 *
 * @param  url      Full https:// URL.
 * @param  tag      Short label for serial log messages, e.g. "weather".
 * @param  handler  Called only on HTTP 200.
 * @param  http10   true (default): HTTP/1.0, so the body is never chunked and
 *                  can be stream-parsed. false: HTTP/1.1 for servers that
 *                  reject 1.0 (answer 426); read the body with getString(),
 *                  which decodes chunked transfer encoding.
 * @param  userAgent  Optional User-Agent override (some APIs require a contact URL).
 * @return true if connected, status 200 and @p handler returned true.
 */
bool get(const String &url, const char *tag, const BodyHandler &handler, bool http10 = true,
         const char *userAgent = nullptr);

}  // namespace Https
