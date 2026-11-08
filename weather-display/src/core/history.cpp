/**
 * @file    history.cpp
 * @brief   LittleFS ring-file implementation of the history log.
 */
#include "history.h"

#include <FS.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"

namespace {

const char *const PATH = "/history.bin";
constexpr uint32_t MAGIC = 0x31485857;  ///< "WXH1".
constexpr uint16_t VERSION = 3;  ///< v2 added Tempest fields, v3 rain rate and carbon.

/** @brief On-disk header (16 bytes). */
struct __attribute__((packed)) HistoryHeader {
  uint32_t magic;       ///< MAGIC.
  uint16_t version;     ///< Layout version; mismatch resets the log.
  uint16_t recordSize;  ///< sizeof(HistoryRecord).
  uint16_t capacity;    ///< Slots in the file.
  uint16_t head;        ///< Next slot to write.
  uint16_t count;       ///< Valid records.
  uint16_t reserved;    ///< Padding.
};

HistoryHeader header{};             ///< Cached copy of the on-disk header.
bool ready = false;                 ///< begin() succeeded.
SemaphoreHandle_t mutex = nullptr;  ///< Serialises file access.

/** @brief Byte offset of slot @p i. */
size_t slotOffset(uint16_t i) { return sizeof(HistoryHeader) + static_cast<size_t>(i) * sizeof(HistoryRecord); }

/** @brief Create an empty, full-size log file. */
bool createFile() {
  File f = LittleFS.open(PATH, "w");
  if (!f) return false;
  header = {MAGIC, VERSION, sizeof(HistoryRecord), HISTORY_CAPACITY, 0, 0, 0};
  f.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
  // Pre-size the file so later writes are in-place.
  uint8_t zeros[64] = {};
  for (size_t left = slotOffset(HISTORY_CAPACITY) - sizeof(header); left > 0;) {
    const size_t n = min(left, sizeof(zeros));
    f.write(zeros, n);
    left -= n;
  }
  f.close();
  Serial.printf("[history] created %s (%u slots x %u B)\n", PATH, HISTORY_CAPACITY, sizeof(HistoryRecord));
  return true;
}

/** @brief Simple RAII wrapper for the module mutex. */
struct Lock {
  Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex); }
};

}  // namespace

namespace History {

bool begin() {
  mutex = xSemaphoreCreateMutex();
  // Partition label "spiffs" from min_spiffs.csv; format on first boot.
  if (!LittleFS.begin(true, "/littlefs", 5, "spiffs")) {
    Serial.println("[history] LittleFS mount failed");
    return false;
  }

  File f = LittleFS.open(PATH, "r");
  bool ok = f && f.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header) && header.magic == MAGIC &&
            header.version == VERSION && header.recordSize == sizeof(HistoryRecord) && header.capacity == HISTORY_CAPACITY &&
            f.size() >= slotOffset(HISTORY_CAPACITY);
  if (f) f.close();
  if (!ok) ok = createFile();  // Missing or incompatible: start fresh.

  ready = ok;
  Serial.printf("[history] %s, %u records, FS %u/%u KB used\n", ok ? "ready" : "unavailable", header.count,
                LittleFS.usedBytes() / 1024, LittleFS.totalBytes() / 1024);
  return ok;
}

bool append(const HistoryRecord &r) {
  if (!ready) return false;
  Lock lock;
  File f = LittleFS.open(PATH, "r+");
  if (!f) return false;
  f.seek(slotOffset(header.head));
  f.write(reinterpret_cast<const uint8_t *>(&r), sizeof(r));
  header.head = (header.head + 1) % header.capacity;
  if (header.count < header.capacity) ++header.count;
  f.seek(0);
  f.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
  f.close();
  return true;
}

uint16_t count() { return header.count; }

uint32_t lastTime() {
  if (!ready || header.count == 0) return 0;
  Lock lock;
  File f = LittleFS.open(PATH, "r");
  if (!f) return 0;
  HistoryRecord r{};
  f.seek(slotOffset((header.head + header.capacity - 1) % header.capacity));
  f.read(reinterpret_cast<uint8_t *>(&r), sizeof(r));
  f.close();
  return r.time;
}

bool read(uint32_t since, std::vector<HistoryRecord> &out) {
  out.clear();
  if (!ready) return false;
  Lock lock;
  File f = LittleFS.open(PATH, "r");
  if (!f) return false;
  out.reserve(header.count);
  // Oldest record is at head when full, else at slot 0.
  const uint16_t first = header.count == header.capacity ? header.head : 0;
  HistoryRecord r;
  for (uint16_t i = 0; i < header.count; ++i) {
    f.seek(slotOffset((first + i) % header.capacity));
    if (f.read(reinterpret_cast<uint8_t *>(&r), sizeof(r)) != sizeof(r)) break;
    if (r.time > since) out.push_back(r);
  }
  f.close();
  return true;
}

}  // namespace History
