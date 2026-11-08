/**
 * @file    web_ui.cpp
 * @brief   HTTP routes, BMP screenshot encoder, JSON/CSV writers.
 */
#include "web_ui.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <esp_heap_caps.h>

#include "battery.h"
#include "config.h"
#include "connectivity.h"
#include "data_service.h"
#include "history.h"
#include "ui.h"
#include "web_page.h"

namespace {

WebServer server(80);           ///< Synchronous server, serviced by its own task.
uint16_t *snapBuf = nullptr;    ///< 240x135 RGB565 screenshot buffer (PSRAM).

constexpr int ROW_BYTES = Ui::WIDTH * 3;  ///< 24-bit BMP row (720 B, already 4-byte aligned).
constexpr int ROWS_PER_SEND = 8;          ///< Rows batched per TCP write.

/** @brief Little-endian writers for the BMP header. */
void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

/** @brief GET /screen.bmp?p=N — render page N off-screen and stream it as a bottom-up 24-bit BMP. */
void handleScreen() {
  const int p = constrain(server.arg("p").toInt(), 0, static_cast<int>(Page::Count) - 1);
  {
    DataService::Guard g;
    const UiModel model{DataService::weather(), DataService::air(),   DataService::warnings(), DataService::aurora(),
                        DataService::radar(),   DataService::flights(), Battery::status()};
    Ui::snapshot(static_cast<Page>(p), model, snapBuf);
  }

  const uint32_t imageSize = ROW_BYTES * Ui::HEIGHT;
  uint8_t hdr[54] = {'B', 'M'};
  put32(hdr + 2, sizeof(hdr) + imageSize);
  put32(hdr + 10, sizeof(hdr));
  put32(hdr + 14, 40);            // BITMAPINFOHEADER
  put32(hdr + 18, Ui::WIDTH);
  put32(hdr + 22, Ui::HEIGHT);    // Positive = bottom-up rows.
  put16(hdr + 26, 1);             // Planes.
  put16(hdr + 28, 24);            // Bits per pixel.
  put32(hdr + 34, imageSize);
  put32(hdr + 38, 2835);          // 72 dpi.
  put32(hdr + 42, 2835);

  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(sizeof(hdr) + imageSize);
  server.send(200, "image/bmp", "");
  server.sendContent(reinterpret_cast<const char *>(hdr), sizeof(hdr));

  static uint8_t rows[ROW_BYTES * ROWS_PER_SEND];
  int filled = 0;
  for (int y = Ui::HEIGHT - 1; y >= 0; --y) {
    uint8_t *d = rows + filled * ROW_BYTES;
    const uint16_t *s = snapBuf + y * Ui::WIDTH;
    for (int x = 0; x < Ui::WIDTH; ++x) {
      const uint16_t c = s[x];
      const uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
      *d++ = (b << 3) | (b >> 2);
      *d++ = (g << 2) | (g >> 4);
      *d++ = (r << 3) | (r >> 2);
    }
    if (++filled == ROWS_PER_SEND || y == 0) {
      server.sendContent(reinterpret_cast<const char *>(rows), filled * ROW_BYTES);
      filled = 0;
    }
  }
}

/** @brief GET /api/now.json — compact summary for the dashboard header. */
void handleNow() {
  JsonDocument doc;
  {
    DataService::Guard g;
    const WeatherData &wx = DataService::weather();
    const AirQualityData &aq = DataService::air();
    const AuroraData &au = DataService::aurora();
    const FlightList &fl = DataService::flights();
    if (wx.valid) {
      doc["temp"] = wx.current.temp;
      doc["feels"] = wx.current.feelsLike;
      doc["condition"] = Weather::describe(wx.current.code);
      doc["humidity"] = wx.current.humidity;
      doc["wind"] = wx.current.wind;
      doc["windDir"] = Weather::compass(wx.current.windDir);
      doc["pressure"] = wx.current.pressure;
      doc["sunrise"] = wx.sunrise;
      doc["sunset"] = wx.sunset;
    }
    if (aq.valid) {
      doc["aqi"] = aq.eaqi;
      doc["aqiBand"] = AirQuality::band(aq.eaqi);
    }
    doc["warnings"] = DataService::warnings().total;
    if (au.valid) doc["aurora"] = Aurora::levelName(au.level);
    if (fl.valid) {
      doc["aircraft"] = fl.total;
      if (fl.nearest >= 0) doc["nearestFlight"] = fl.items[fl.nearest].callsign;
    }
  }
  const BatteryStatus &b = Battery::status();
  if (b.present) doc["battery"] = static_cast<int>(lroundf(b.percent));
  doc["rssi"] = Net::rssi();
  doc["ip"] = Net::ip();
  doc["uptimeMin"] = millis() / 60000;
  doc["historyCount"] = History::count();
  doc["historyIntervalMin"] = HISTORY_INTERVAL_S / 60;
  JsonArray pages = doc["pages"].to<JsonArray>();
  for (uint8_t i = 0; i < static_cast<uint8_t>(Page::Count); ++i) pages.add(Ui::pageName(static_cast<Page>(i)));

  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

/** @brief GET /history.csv?days=N — stream the log as CSV (chunked). */
void handleHistory() {
  const int days = constrain(server.hasArg("days") ? server.arg("days").toInt() : HISTORY_DEFAULT_DAYS, 1, 31);
  const time_t now = time(nullptr);
  const uint32_t since = now > days * 86400L ? now - days * 86400L : 0;

  std::vector<HistoryRecord> recs;
  History::read(since, recs);  // Copy out first so the file lock isn't held while sending.

  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", "inline; filename=\"weather-history.csv\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv",
              "epoch,local_time,temp_c,feels_c,humidity_pct,pressure_hpa,rain_mm,wind_mph,gust_mph,wind_dir,"
              "weather_code,eaqi,pm25,battery_pct,battery_v,rssi_dbm,aircraft,aurora_nt,aurora_level\n");

  String chunk;
  chunk.reserve(4096);
  char line[200], when[20];
  for (const HistoryRecord &r : recs) {
    const time_t t = r.time;
    tm lt;
    localtime_r(&t, &lt);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &lt);
    const bool w = r.flags & HF_WEATHER, a = r.flags & HF_AIR, b = r.flags & HF_BATTERY, au = r.flags & HF_AURORA;
    char wx[96] = ",,,,,,,,", air[24] = ",", batt[24] = ",", aur[24] = ",";
    if (w)
      snprintf(wx, sizeof(wx), "%.1f,%.1f,%u,%.1f,%.2f,%.1f,%.1f,%u,%u", r.temp10 / 10.0, r.feels10 / 10.0, r.humidity,
               r.pressure10 / 10.0, r.rain100 / 100.0, r.wind10 / 10.0, r.gust10 / 10.0, r.windDir, r.code);
    if (a) snprintf(air, sizeof(air), "%u,%.1f", r.eaqi, r.pm25x10 / 10.0);
    if (b) snprintf(batt, sizeof(batt), "%u,%.3f", r.battPct, r.battMv / 1000.0);
    if (au) snprintf(aur, sizeof(aur), "%.1f,%u", r.aurora10 / 10.0, r.auroraLevel);
    snprintf(line, sizeof(line), "%lu,%s,%s,%s,%s,%d,%u,%s\n", static_cast<unsigned long>(r.time), when, wx, air, batt,
             r.rssi, r.aircraft, aur);
    chunk += line;
    if (chunk.length() > 3500) {
      server.sendContent(chunk);
      chunk = "";
    }
  }
  if (chunk.length()) server.sendContent(chunk);
  server.sendContent("");  // End of chunked response.
}

/** @brief Server task: service clients forever. */
void serverTask(void *) {
  for (;;) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

}  // namespace

namespace WebUi {

void begin() {
  snapBuf = static_cast<uint16_t *>(heap_caps_malloc(Ui::WIDTH * Ui::HEIGHT * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!snapBuf) {
    Serial.println("[web] no memory for screenshot buffer");
    return;
  }

  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, [] { server.send(200, "text/html", WEB_PAGE); });
  server.on("/screen.bmp", HTTP_GET, handleScreen);
  server.on("/api/now.json", HTTP_GET, handleNow);
  server.on("/history.csv", HTTP_GET, handleHistory);
  server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
  server.begin();

  xTaskCreatePinnedToCore(serverTask, "web", 8192, nullptr, 1, nullptr, 1);
  Serial.printf("[web] http://%s.local/  (http://%s/)\n", HOSTNAME, Net::ip().c_str());
}

}  // namespace WebUi
