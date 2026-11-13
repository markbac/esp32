/**
 * @file    log.h
 * @brief   Logging to serial, a RAM tail and (CrowPanel) daily files on the SD card.
 *
 * @details Every Log::printf / Log::println line goes to the serial port as
 *          before, into a 16 KB ring in RAM (shown live at /logs even with no
 *          card), and - when an SD card is mounted - into a buffer that the UI
 *          loop writes out every few seconds:
 *
 *          | File                         | Contents                                     |
 *          |------------------------------|----------------------------------------------|
 *          | /logs/app-YYYY-MM-DD.log     | Debug log, "HH:MM:SS [module] message"       |
 *          | /logs/net-YYYY-MM-DD.csv     | Wi-Fi tests, monitor minutes, scans, drops   |
 *
 *          Files older than LOG_KEEP_DAYS are deleted at start-up and at
 *          midnight. Any task may log; only flush() (UI loop) touches the card.
 *
 *          The web routes are /logs (file list + live tail), /logs/file?name=
 *          (view or ?dl=1 download), /logs/tail (plain text) and /api/logs.
 */
#pragma once

#include <Arduino.h>

class WebServer;

/** @brief Days of log files kept on the card. */
constexpr uint16_t LOG_KEEP_DAYS = 60;

/** @brief CSV header for the network log. */
constexpr const char *NET_LOG_HEADER =
    "time,type,rssi,router_avg_ms,router_max_ms,router_lost,router_sent,internet_avg_ms,internet_max_ms,"
    "internet_lost,internet_sent,spikes,dropouts,down_mbps,verdict,devices,other_devices,busy_pct,retry_pct,dns_ms,"
    "roams,detail";

namespace Log {

/**
 * @brief  Mount the SD card (if this board has one) and prune old files.
 * @return true if the card is ready for logging.
 */
bool begin();

/**
 * @brief Log why the board last restarted and, after a crash, the crashed task and
 *        its backtrace from the core dump in flash (then erase it so it is reported
 *        once). Decode the addresses with:
 *        xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/<env>/firmware.elf <addresses>
 */
void bootReport();

/** @brief printf to serial, the RAM tail and the SD app log. */
void printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** @brief One line (newline added). */
void println(const char *s);
inline void println(const String &s) { println(s.c_str()); }

/** @brief Append one CSV row (without time; it is prefixed) to the network log. */
void net(const char *row);

/** @brief Write buffered lines to the card (call every few seconds from the UI loop). */
void flush();

/** @brief True if an SD card is mounted. */
bool sdReady();

/** @brief Card size and space used, bytes (0 when no card). */
uint64_t sdTotal();
uint64_t sdUsed();

/** @brief Register the /logs pages on @p server. */
void registerRoutes(WebServer &server);

}  // namespace Log
