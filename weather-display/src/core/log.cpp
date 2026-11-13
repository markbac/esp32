/**
 * @file    log.cpp
 * @brief   Serial + RAM tail + SD card logging, and the /logs web pages.
 */
#include "log.h"

#include <WebServer.h>

#include <algorithm>
#include <esp_core_dump.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <time.h>

#include "config.h"

#if ENABLE_SD_LOG
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#endif

#ifndef SD_CS_PIN
#define SD_CS_PIN 10
#endif
#ifndef SD_MOSI_PIN
#define SD_MOSI_PIN 11
#endif
#ifndef SD_SCK_PIN
#define SD_SCK_PIN 12
#endif
#ifndef SD_MISO_PIN
#define SD_MISO_PIN 13
#endif

namespace {

SemaphoreHandle_t mutex = nullptr;

struct Lock {
  Lock() {
    if (!mutex) mutex = xSemaphoreCreateMutex();
    xSemaphoreTake(mutex, portMAX_DELAY);
  }
  ~Lock() { xSemaphoreGive(mutex); }
};

/** @brief Recent log text, circular. */
constexpr size_t TAIL_SIZE = 16 * 1024;
char *tail = nullptr;
size_t tailHead = 0;   ///< Next write position.
bool tailWrapped = false;

/** @brief Lines waiting to be written to the card. */
String pendingApp, pendingNet;
constexpr size_t PENDING_MAX = 48 * 1024;  ///< Drop (and count) beyond this if the card stalls.
uint32_t dropped = 0;

bool sdOk = false;
#if ENABLE_SD_LOG
int lastPruneDay = -1;
#endif
WebServer *srv = nullptr;

void tailAppend(const char *s, size_t n) {
  if (!tail) {
    tail = static_cast<char *>(heap_caps_malloc(TAIL_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!tail) return;
  }
  for (size_t i = 0; i < n; ++i) {
    tail[tailHead++] = s[i];
    if (tailHead == TAIL_SIZE) {
      tailHead = 0;
      tailWrapped = true;
    }
  }
}

/** @brief "HH:MM:SS" (or uptime seconds before the clock is set). */
void stamp(char *buf, size_t n) {
  const time_t now = time(nullptr);
  if (now > 1700000000) {
    tm t;
    localtime_r(&now, &t);
    strftime(buf, n, "%H:%M:%S", &t);
  } else {
    snprintf(buf, n, "+%lus", static_cast<unsigned long>(millis() / 1000));
  }
}

/** @brief Record one complete line (already on serial). */
void record(const char *line) {
  char ts[16];
  stamp(ts, sizeof(ts));
  Lock l;
  tailAppend(ts, strlen(ts));
  tailAppend(" ", 1);
  const size_t n = strlen(line);
  tailAppend(line, n);
  if (!n || line[n - 1] != '\n') tailAppend("\n", 1);
  if (!sdOk) return;
  if (pendingApp.length() > PENDING_MAX) {
    ++dropped;
    return;
  }
  pendingApp += ts;
  pendingApp += ' ';
  pendingApp += line;
  if (!n || line[n - 1] != '\n') pendingApp += '\n';
}

#if ENABLE_SD_LOG
/** @brief Local date "YYYY-MM-DD" (false before the clock is set). */
bool today(char *buf, size_t n, int *yday = nullptr) {
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;
  tm t;
  localtime_r(&now, &t);
  strftime(buf, n, "%Y-%m-%d", &t);
  if (yday) *yday = t.tm_year * 400 + t.tm_yday;
  return true;
}

/** @brief Only plain names inside /logs may be served. */
bool safeName(const String &n) {
  if (n.isEmpty() || n.length() > 40) return false;
  for (char c : n) {
    if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') return false;
  }
  return !n.startsWith(".");
}

SPIClass sdSpi(FSPI);

/** @brief Delete log files older than LOG_KEEP_DAYS (names carry the date). */
void prune() {
  const time_t cutoff = time(nullptr) - static_cast<time_t>(LOG_KEEP_DAYS) * 86400;
  tm t;
  localtime_r(&cutoff, &t);
  char oldest[11];
  strftime(oldest, sizeof(oldest), "%Y-%m-%d", &t);
  File dir = SD.open("/logs");
  if (!dir) return;
  String doomed[16];
  int n = 0;
  for (File f = dir.openNextFile(); f && n < 16; f = dir.openNextFile()) {
    const String name = f.name();
    const int dash = name.indexOf('-');
    if (dash > 0 && name.length() >= static_cast<unsigned>(dash + 11) && name.substring(dash + 1, dash + 11) < oldest)
      doomed[n++] = name;
  }
  dir.close();
  for (int i = 0; i < n; ++i) {
    SD.remove("/logs/" + doomed[i]);
    Serial.printf("[log] removed old %s\n", doomed[i].c_str());
  }
}

bool append(const char *path, const String &data, const char *header) {
  const bool fresh = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) return false;
  if (fresh && header) {
    f.print(header);
    f.print('\n');
  }
  const size_t w = f.print(data);
  f.close();
  return w == data.length();
}
#endif

// ---------------------------------------------------------------- web

void sendTail() {
  String out;
  {
    Lock l;
    if (!tail) {
      out = "";
    } else if (!tailWrapped) {
      out.concat(tail, tailHead);
    } else {
      out.reserve(TAIL_SIZE);
      out.concat(tail + tailHead, TAIL_SIZE - tailHead);
      out.concat(tail, tailHead);
      const int nl = out.indexOf('\n');  // Drop the partial first line.
      if (nl >= 0) out.remove(0, nl + 1);
    }
  }
  srv->sendHeader("Cache-Control", "no-store");
  srv->send(200, "text/plain; charset=utf-8", out);
}

void sendList() {
  String h;
  h.reserve(6000);
  h += F("<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>Logs</title><style>"
         ":root{--bg:#0d1117;--card:#161b22;--line:#30363d;--text:#e6edf3;--muted:#8b949e;--accent:#58a6ff}"
         "body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,sans-serif}"
         "main{max-width:980px;margin:0 auto;padding:20px}a{color:var(--accent)}h1{font-size:20px}"
         "table{border-collapse:collapse;width:100%;background:var(--card);border-radius:10px}"
         "td,th{padding:6px 10px;border-bottom:1px solid var(--line);text-align:left}th{color:var(--muted);font-weight:600}"
         "pre{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px;height:420px;overflow:auto;"
         "font:12px/1.35 ui-monospace,monospace;white-space:pre-wrap}.muted{color:var(--muted)}"
         "button{background:var(--accent);color:#0d1117;border:0;border-radius:6px;padding:8px 14px;font:inherit;font-weight:600;cursor:pointer}"
         "</style></head><body><main><h1>Logs &middot; " HOSTNAME ".local</h1><p><a href='/'>&larr; Dashboard</a></p>");
#if ENABLE_SD_LOG
  if (sdOk) {
    char b[96];
    snprintf(b, sizeof(b), "<p class=muted>SD card: %.1f of %.1f GB used. Files older than %u days are deleted.</p>",
             SD.usedBytes() / 1e9, SD.totalBytes() / 1e9, LOG_KEEP_DAYS);
    h += b;
    h += F("<table><tr><th>File</th><th>Size</th><th></th></tr>");
    File dir = SD.open("/logs");
    String rows[90];
    int n = 0;
    for (File f = dir ? dir.openNextFile() : File(); f && n < 90; f = dir.openNextFile()) {
      if (f.isDirectory()) continue;
      const String name = f.name();
      rows[n++] = name + "|" + String(static_cast<unsigned long>(f.size()));
    }
    if (dir) dir.close();
    // Newest first (names sort by date within each type).
    std::sort(rows, rows + n, [](const String &a, const String &b) {
      return a.substring(a.indexOf('-')) > b.substring(b.indexOf('-')) ||
             (a.substring(a.indexOf('-')) == b.substring(b.indexOf('-')) && a < b);
    });
    for (int i = 0; i < n; ++i) {
      const int bar = rows[i].indexOf('|');
      const String name = rows[i].substring(0, bar);
      const unsigned long size = rows[i].substring(bar + 1).toInt();
      h += "<tr><td><a href='/logs/file?name=" + name + "'>" + name + "</a></td><td>" +
           (size >= 1024 ? String(size / 1024) + " KB" : String(size) + " B") +
           "</td><td><a href='/logs/file?name=" + name + "&dl=1'>download</a></td></tr>";
    }
    if (!n) h += F("<tr><td colspan=3 class=muted>No log files yet</td></tr>");
    h += F("</table>");
  } else {
    h += F("<p class=muted>No SD card found: showing the live log only. Insert a FAT32 card and restart to keep logs.</p>");
  }
#else
  h += F("<p class=muted>This board has no SD card: showing the live log only.</p>");
#endif
#if ENABLE_NET_TOOLS
  h += F("<h2 style='font-size:16px'>Wi-Fi test</h2><p>"
         "<button onclick=\"nt('action=test')\">Quick test</button> "
         "<button onclick=\"nt('action=air')\">Air survey 20 s</button> "
         "<button onclick=\"nt('action=monitor&minutes=60')\">Monitor 1 h</button> "
         "<button onclick=\"nt('action=monitor&minutes=240')\">Monitor 4 h</button> "
         "<button onclick=\"nt('action=stop')\">Stop</button> <span id=ns class=muted></span></p>"
         "<pre id=nr style='height:auto;min-height:3em'></pre><script>"
         "async function nt(b){const r=await fetch('/api/nettest',{method:'POST',headers:{'Content-Type':"
         "'application/x-www-form-urlencoded'},body:b});document.getElementById('ns').textContent=r.ok?'started':'busy or offline'}"
         "async function nr(){const j=await (await fetch('/api/now.json')).json();"
         "document.getElementById('nr').textContent=JSON.stringify({test:j.netTest,air:j.airSurvey,monitor:j.monitor},null,1)||''}"
         "nr();setInterval(nr,5000)</script>");
#endif
  h += F("<h2 style='font-size:16px'>Live log <span class=muted>(last 16 KB, refreshes every 5 s)</span></h2>"
         "<pre id=t>Loading...</pre><script>"
         "async function r(){const p=document.getElementById('t');const end=p.scrollTop+p.clientHeight>=p.scrollHeight-20;"
         "p.textContent=await (await fetch('/logs/tail')).text();if(end)p.scrollTop=p.scrollHeight}"
         "r();setInterval(r,5000)</script></main></body></html>");
  srv->sendHeader("Cache-Control", "no-store");
  srv->send(200, "text/html", h);
}

void sendFile() {
#if ENABLE_SD_LOG
  const String name = srv->arg("name");
  if (!sdOk || !safeName(name)) {
    srv->send(404, "text/plain", "Not found");
    return;
  }
  Log::flush();  // Include the latest lines.
  File f = SD.open("/logs/" + name);
  if (!f || f.isDirectory()) {
    srv->send(404, "text/plain", "Not found");
    return;
  }
  if (srv->hasArg("dl")) srv->sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  srv->sendHeader("Cache-Control", "no-store");
  srv->streamFile(f, "text/plain; charset=utf-8");
  f.close();
#else
  srv->send(404, "text/plain", "No SD card on this board");
#endif
}

void sendApi() {
  String out = "{\"sd\":";
  out += sdOk ? "true" : "false";
#if ENABLE_SD_LOG
  if (sdOk) {
    out += ",\"totalBytes\":" + String(static_cast<double>(SD.totalBytes()), 0);
    out += ",\"usedBytes\":" + String(static_cast<double>(SD.usedBytes()), 0);
    out += ",\"files\":[";
    File dir = SD.open("/logs");
    bool first = true;
    for (File f = dir ? dir.openNextFile() : File(); f; f = dir.openNextFile()) {
      if (f.isDirectory()) continue;
      out += String(first ? "" : ",") + "{\"name\":\"" + f.name() + "\",\"size\":" + String(static_cast<unsigned long>(f.size())) + "}";
      first = false;
    }
    if (dir) dir.close();
    out += "]";
  }
#endif
  out += ",\"dropped\":" + String(dropped) + "}";
  srv->sendHeader("Cache-Control", "no-store");
  srv->send(200, "application/json", out);
}

}  // namespace

namespace Log {

bool begin() {
#if ENABLE_SD_LOG
  sdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  bool ok = SD.begin(SD_CS_PIN, sdSpi, 20000000) && SD.cardType() != CARD_NONE;
  if (!ok) {  // Some cards only start at a slow clock.
    SD.end();
    ok = SD.begin(SD_CS_PIN, sdSpi, 4000000) && SD.cardType() != CARD_NONE;
  }
  if (!ok) {
    Log::println("[log] no SD card (or not FAT32) - logging to serial and RAM only");
    return false;
  }
  if (!SD.exists("/logs")) SD.mkdir("/logs");
  sdOk = true;
  Log::printf("[log] SD card %.1f GB, %.1f GB used; writing /logs\n", SD.totalBytes() / 1e9, SD.usedBytes() / 1e9);
  return true;
#else
  return false;
#endif
}

void bootReport() {
  const char *why = "unknown";
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: why = "power on"; break;
    case ESP_RST_EXT: why = "reset pin"; break;
    case ESP_RST_SW: why = "software restart"; break;
    case ESP_RST_PANIC: why = "CRASH (exception / panic)"; break;
    case ESP_RST_INT_WDT: why = "CRASH (interrupt watchdog)"; break;
    case ESP_RST_TASK_WDT: why = "CRASH (task watchdog)"; break;
    case ESP_RST_WDT: why = "CRASH (other watchdog)"; break;
    case ESP_RST_DEEPSLEEP: why = "wake from deep sleep"; break;
    case ESP_RST_BROWNOUT: why = "BROWNOUT (power supply dipped)"; break;
    case ESP_RST_SDIO: why = "SDIO"; break;
    default: break;
  }
  Log::printf("[boot] reset reason: %s\n", why);
  if (esp_core_dump_image_check() != ESP_OK) return;  // No (valid) crash dump stored.
  auto *sum = static_cast<esp_core_dump_summary_t *>(malloc(sizeof(esp_core_dump_summary_t)));
  if (sum && esp_core_dump_get_summary(sum) == ESP_OK) {
    char bt[16 * 11 + 1] = "";
    for (uint32_t i = 0; i < sum->exc_bt_info.depth && i < 16; ++i) {
      char a[12];
      snprintf(a, sizeof(a), " 0x%08lx", static_cast<unsigned long>(sum->exc_bt_info.bt[i]));
      strlcat(bt, a, sizeof(bt));
    }
    Log::printf("[boot] last crash in task '%s' at pc 0x%08lx, cause %lu%s\n", sum->exc_task,
                static_cast<unsigned long>(sum->exc_pc), static_cast<unsigned long>(sum->ex_info.exc_cause),
                sum->exc_bt_info.corrupted ? " (backtrace corrupted)" : "");
    Log::printf("[boot] backtrace:%s\n", bt);
  }
  free(sum);
  esp_core_dump_image_erase();  // Report each crash once.
}

void printf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  Serial.print(buf);
  record(buf);
}

void println(const char *s) {
  Serial.println(s);
  record(s);
}

void net(JsonDocument &doc) {
  char ts[24];
  const time_t now = time(nullptr);
  tm t;
  localtime_r(&now, &t);
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &t);
  doc["time"] = ts;
  String line;
  serializeJson(doc, line);
  Lock l;
  if (!sdOk || pendingNet.length() > PENDING_MAX) return;
  pendingNet += line;
  pendingNet += '\n';
}

void flush() {
#if ENABLE_SD_LOG
  if (!sdOk) return;
  char date[12];
  int yday = 0;
  if (!today(date, sizeof(date), &yday)) return;  // Keep buffering until the clock is set.
  String app, netRows;
  {
    Lock l;
    app = pendingApp;
    netRows = pendingNet;
    pendingApp = "";
    pendingNet = "";
  }
  if (yday != lastPruneDay) {
    lastPruneDay = yday;
    prune();
  }
  if (app.length() && !append((String("/logs/app-") + date + ".log").c_str(), app, nullptr)) {
    Serial.println("[log] SD write failed - card removed? logging to RAM only");
    sdOk = false;
  }
  if (sdOk && netRows.length()) append((String("/logs/net-") + date + ".jsonl").c_str(), netRows, nullptr);
#endif
}

bool sdReady() { return sdOk; }

uint64_t sdTotal() {
#if ENABLE_SD_LOG
  return sdOk ? SD.totalBytes() : 0;
#else
  return 0;
#endif
}

uint64_t sdUsed() {
#if ENABLE_SD_LOG
  return sdOk ? SD.usedBytes() : 0;
#else
  return 0;
#endif
}

void registerRoutes(WebServer &server) {
  srv = &server;
  server.on("/logs", HTTP_GET, sendList);
  server.on("/logs/tail", HTTP_GET, sendTail);
  server.on("/logs/file", HTTP_GET, sendFile);
  server.on("/api/logs", HTTP_GET, sendApi);
}

}  // namespace Log
