/**
 * @file    web_ui.h
 * @brief   Local web dashboard: live screen mirrors and history charts.
 *
 * @details Served on port 80 and advertised as http://HOSTNAME.local (mDNS).
 *          Endpoints:
 *          | Path                     | Content                                        |
 *          |--------------------------|------------------------------------------------|
 *          | `/`                      | Dashboard HTML (screens + charts)              |
 *          | `/screen.bmp?p=N`        | Live 240x135 screenshot of page N (24-bit BMP) |
 *          | `/api/now.json`          | Current values summary                         |
 *          | `/history.csv?days=7`    | Logged history, oldest first (CSV)             |
 *
 *          Runs in its own FreeRTOS task on core 1 so slow clients never stall
 *          the display loop. Read-only, LAN only, no authentication.
 */
#pragma once

namespace WebUi {

/** @brief Start mDNS, register routes and launch the server task (call after Wi-Fi is up). */
void begin();

}  // namespace WebUi
