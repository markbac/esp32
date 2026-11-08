/**
 * @file    web_ui.h
 * @brief   Local web dashboard: live screen mirrors and history charts.
 *
 * @details Served on port 80 and advertised as http://HOSTNAME.local (mDNS).
 *          Endpoints:
 *          | Path                     | Content                                        |
 *          |--------------------------|------------------------------------------------|
 *          | `/`                      | Dashboard HTML (screens + charts)              |
 *          | `/screen.bmp?p=N`        | Live screenshot of page N (24-bit BMP)         |
 *          | `/api/now.json`          | Current values summary                         |
 *          | `/history.csv?days=7`    | Logged history, oldest first (CSV)             |
 *
 *          Shared by both boards: each board supplies a @ref WebUi::Board
 *          describing its pages and how to snapshot them. Runs in its own
 *          FreeRTOS task on core 1. Read-only, LAN only, no authentication.
 */
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace WebUi {

/** @brief Board-specific hooks used by the web server. */
struct Board {
  const char *name;                   ///< e.g. "Feather ESP32-S3 Reverse TFT".
  uint16_t width;                     ///< Snapshot width served, pixels.
  uint16_t height;                    ///< Snapshot height served, pixels.
  uint8_t pageCount;                  ///< Number of pages.
  const char *(*pageName)(uint8_t);   ///< Page index -> name.
  /**
   * @brief Render page @p page off-screen into @p out (width*height native
   *        RGB565). Must take whatever locks it needs; never touch the panel.
   */
  void (*snapshot)(uint8_t page, uint16_t *out);
  /** @brief Optional: add board-specific fields to /api/now.json. */
  void (*extraJson)(JsonDocument &doc);
};

/** @brief Start mDNS, register routes and launch the server task (call after Wi-Fi is up). */
void begin(const Board &board);

}  // namespace WebUi
