/**
 * @file    secrets.example.h
 * @brief   Template for Wi-Fi credentials.
 *
 * @details Copy this file to secrets.h (git-ignored) and fill in your network.
 *          If secrets.h is missing the build falls back to this file, so the
 *          project compiles but won't connect. The ESP32-S3 only supports
 *          2.4 GHz Wi-Fi.
 */
#pragma once

/** @brief Wi-Fi network name (SSID). */
#define WIFI_SSID      "YOUR_WIFI_NAME"

/** @brief Wi-Fi password. */
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

/**
 * @brief Optional Home Assistant long-lived access token (enables the Home page).
 *        Create one in Home Assistant: your profile -> Security -> Long-lived access tokens.
 */
// #define HA_TOKEN "paste-your-long-lived-access-token-here"
