/**
 * @file    web_settings.h
 * @brief   Web settings page: GET/POST /settings and GET /api/settings.
 *
 * @details Secrets (Wi-Fi password, Home Assistant token, admin password) are
 *          write-only: the page shows "set"/"not set", a blank field keeps the
 *          current value and a checkbox clears it. Nothing secret is ever
 *          rendered into HTML or JSON. When an admin password is set, saving
 *          requires it. Wi-Fi changes apply after the device restarts.
 */
#pragma once

class WebServer;

namespace WebSettings {

/** @brief Register the settings routes on @p server. */
void registerRoutes(WebServer &server);

}  // namespace WebSettings
