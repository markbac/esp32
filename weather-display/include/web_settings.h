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

#include <stdint.h>

class WebServer;

namespace WebSettings {

/**
 * @brief Let the settings page offer a checkbox per page for auto paging.
 * @param count      Pages on this board.
 * @param name       Page name by index.
 * @param rotatable  Whether a page may be included (others are not listed).
 */
void setPages(uint8_t count, const char *(*name)(uint8_t), bool (*rotatable)(uint8_t));

/** @brief Register the settings routes on @p server. */
void registerRoutes(WebServer &server);

}  // namespace WebSettings
