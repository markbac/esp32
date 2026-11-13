/**
 * @file    history.cpp
 * @brief   Append-only weekly-file implementation of the history log (LittleFS).
 */
#include "history.h"
#include "log.h"

#include <FS.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <algorithm>

#include "config.h"

namespace {

const char *const DIR = "/hist";
const char *const META = "/hist/meta.bin";
const char *const LEGACY = "/history.bin";   ///< Old single ring file (v1-v3), removed on boot.
constexpr uint32_t MAGIC = 0x34485857;       ///< "WXH4".
constexpr uint16_t VERSION = 4;              ///< v4: weekly append-only files (record layout as v3).
constexpr uint32_t WEEK_S = 7UL * 86400UL;

/** @brief Contents of meta.bin. */
struct __attribute__((packed)) Meta {
  uint32_t magic;
  uint16_t version;
  uint16_t recordSize;
};

bool ready = false;
uint32_t recordCount = 0;            ///< Records across all files.
SemaphoreHandle_t mutex = nullptr;   ///< Serialises file access.

struct Lock {
  Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex); }
};

String weekPath(uint32_t week) { return String(DIR) + "/w" + week + ".bin"; }

/** @brief Week numbers of all log files, ascending. */
std::vector<uint32_t> listWeeks() {
  std::vector<uint32_t> weeks;
  File dir = LittleFS.open(DIR);
  if (!dir || !dir.isDirectory()) return weeks;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char *name = f.name();  // e.g. "w2914.bin"
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (base[0] == 'w') weeks.push_back(strtoul(base + 1, nullptr, 10));
  }
  std::sort(weeks.begin(), weeks.end());
  return weeks;
}

/** @brief Delete every log file and write fresh metadata. */
void resetLog() {
  for (uint32_t w : listWeeks()) LittleFS.remove(weekPath(w));
  LittleFS.mkdir(DIR);
  File m = LittleFS.open(META, "w");
  const Meta meta{MAGIC, VERSION, sizeof(HistoryRecord)};
  if (m) {
    m.write(reinterpret_cast<const uint8_t *>(&meta), sizeof(meta));
    m.close();
  }
  recordCount = 0;
}

/** @brief Remove whole weeks older than HISTORY_DAYS (plus the partial current week). */
void prune(uint32_t currentWeek) {
  const uint32_t keepWeeks = (HISTORY_DAYS + 6) / 7 + 1;
  for (uint32_t w : listWeeks()) {
    if (currentWeek >= keepWeeks && w <= currentWeek - keepWeeks) {
      File f = LittleFS.open(weekPath(w), "r");
      const size_t n = f ? f.size() / sizeof(HistoryRecord) : 0;
      if (f) f.close();
      LittleFS.remove(weekPath(w));
      recordCount -= min<uint32_t>(recordCount, n);
      Log::printf("[history] pruned week %lu (%u records)\n", static_cast<unsigned long>(w), static_cast<unsigned>(n));
    }
  }
}

}  // namespace

namespace History {

bool begin() {
  mutex = xSemaphoreCreateMutex();
  // Partition label "spiffs" from the partition table; format on first boot.
  if (!LittleFS.begin(true, "/littlefs", 5, "spiffs")) {
    Log::println("[history] LittleFS mount failed");
    return false;
  }
  if (LittleFS.exists(LEGACY)) {
    LittleFS.remove(LEGACY);
    Log::println("[history] removed legacy ring file");
  }
  LittleFS.mkdir(DIR);

  Meta meta{};
  File m = LittleFS.open(META, "r");
  const bool metaOk = m && m.read(reinterpret_cast<uint8_t *>(&meta), sizeof(meta)) == sizeof(meta) &&
                      meta.magic == MAGIC && meta.version == VERSION && meta.recordSize == sizeof(HistoryRecord);
  if (m) m.close();
  if (!metaOk) resetLog();

  recordCount = 0;
  for (uint32_t w : listWeeks()) {
    File f = LittleFS.open(weekPath(w), "r");
    if (f) {
      recordCount += f.size() / sizeof(HistoryRecord);
      f.close();
    }
  }
  ready = true;
  Log::printf("[history] ready, %lu records in %u weekly files, FS %u/%u KB used\n",
                static_cast<unsigned long>(recordCount), static_cast<unsigned>(listWeeks().size()),
                LittleFS.usedBytes() / 1024, LittleFS.totalBytes() / 1024);
  return true;
}

bool append(const HistoryRecord &r) {
  if (!ready) return false;
  Lock lock;
  const uint32_t week = r.time / WEEK_S;
  File f = LittleFS.open(weekPath(week), "a");
  if (!f) return false;
  const bool ok = f.write(reinterpret_cast<const uint8_t *>(&r), sizeof(r)) == sizeof(r);
  f.close();
  if (ok) ++recordCount;
  prune(week);
  return ok;
}

uint16_t count() { return static_cast<uint16_t>(min<uint32_t>(recordCount, UINT16_MAX)); }

uint32_t lastTime() {
  if (!ready) return 0;
  Lock lock;
  const std::vector<uint32_t> weeks = listWeeks();
  for (auto it = weeks.rbegin(); it != weeks.rend(); ++it) {
    File f = LittleFS.open(weekPath(*it), "r");
    if (!f) continue;
    const size_t n = f.size() / sizeof(HistoryRecord);
    HistoryRecord r{};
    if (n && f.seek((n - 1) * sizeof(HistoryRecord)) && f.read(reinterpret_cast<uint8_t *>(&r), sizeof(r)) == sizeof(r)) {
      f.close();
      return r.time;
    }
    f.close();
  }
  return 0;
}

bool read(uint32_t since, std::vector<HistoryRecord> &out) {
  out.clear();
  if (!ready) return false;
  Lock lock;
  const uint32_t firstWeek = since / WEEK_S;
  HistoryRecord r;
  for (uint32_t w : listWeeks()) {
    if (w < firstWeek) continue;
    File f = LittleFS.open(weekPath(w), "r");
    if (!f) continue;
    out.reserve(out.size() + f.size() / sizeof(HistoryRecord));
    while (f.read(reinterpret_cast<uint8_t *>(&r), sizeof(r)) == sizeof(r)) {
      if (r.time > since) out.push_back(r);
    }
    f.close();
  }
  return true;
}

}  // namespace History
