/**
 * @file    web_ui.cpp
 * @brief   HTTP routes, BMP screenshot encoder, JSON/CSV writers.
 */
#include "web_ui.h"
#include "log.h"
#include "net_test.h"

#include <ESPmDNS.h>
#include <WebServer.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "connectivity.h"
#include "data_service.h"
#include "history.h"
#include "web_page.h"
#include "web_settings.h"
#include "web_api.h"

namespace {

WebServer server(80);         ///< Synchronous server, serviced by its own task.
WebUi::Board board{};         ///< Hooks supplied by the board.
uint16_t *snapBuf = nullptr;  ///< width*height RGB565 screenshot buffer (PSRAM).

constexpr int ROWS_PER_SEND = 32;  ///< BMP rows batched per TCP write (fewer, larger writes are faster).

/** @brief Little-endian writers for the BMP header. */
void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

/** @brief GET /screen.bmp?p=N — render page N off-screen and stream it as a bottom-up 24-bit BMP. */
void handleScreen() {
  const int p = constrain(server.arg("p").toInt(), 0, board.pageCount - 1);
  board.snapshot(static_cast<uint8_t>(p), snapBuf);

  const int w = board.width, h = board.height;
  const int rowBytes = (w * 3 + 3) & ~3;  // Rows padded to 4 bytes.
  const uint32_t imageSize = rowBytes * h;
  uint8_t hdr[54] = {'B', 'M'};
  put32(hdr + 2, sizeof(hdr) + imageSize);
  put32(hdr + 10, sizeof(hdr));
  put32(hdr + 14, 40);  // BITMAPINFOHEADER
  put32(hdr + 18, w);
  put32(hdr + 22, h);   // Positive = bottom-up rows.
  put16(hdr + 26, 1);
  put16(hdr + 28, 24);
  put32(hdr + 34, imageSize);
  put32(hdr + 38, 2835);
  put32(hdr + 42, 2835);

  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(sizeof(hdr) + imageSize);
  server.send(200, "image/bmp", "");
  server.sendContent(reinterpret_cast<const char *>(hdr), sizeof(hdr));

  uint8_t *rows = static_cast<uint8_t *>(heap_caps_malloc(rowBytes * ROWS_PER_SEND, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!rows) return;
  int filled = 0;
  for (int y = h - 1; y >= 0; --y) {
    uint8_t *d = rows + filled * rowBytes;
    const uint16_t *s = snapBuf + y * w;
    for (int x = 0; x < w; ++x) {
      const uint16_t c = s[x];
      const uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
      *d++ = (b << 3) | (b >> 2);
      *d++ = (g << 2) | (g >> 4);
      *d++ = (r << 3) | (r >> 2);
    }
    if (++filled == ROWS_PER_SEND || y == 0) {
      server.sendContent(reinterpret_cast<const char *>(rows), filled * rowBytes);
      filled = 0;
    }
  }
  free(rows);
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
    const TempestData &t = DataService::tempest();
    if (t.fresh()) {
      doc["temp"] = t.obs.tempC;
      doc["feels"] = t.feelsC;
      doc["humidity"] = t.obs.humidity;
      doc["wind"] = t.obs.windAvg;
      doc["windDir"] = Weather::compass(t.obs.windDir);
      doc["pressure"] = t.obs.pressureSea;
      doc["rainToday"] = t.rainTodayMm;
      doc["source"] = "Tempest";
    } else if (wx.valid) {
      doc["temp"] = wx.current.temp;
      doc["feels"] = wx.current.feelsLike;
      doc["humidity"] = wx.current.humidity;
      doc["wind"] = wx.current.wind;
      doc["windDir"] = Weather::compass(wx.current.windDir);
      doc["pressure"] = wx.current.pressure;
      doc["source"] = "Open-Meteo";
    }
    if (wx.valid) {
      doc["condition"] = Weather::describe(wx.current.code);
      doc["sunrise"] = wx.sunrise;
      doc["sunset"] = wx.sunset;
    }
    const CarbonData &ci = DataService::carbon();
    if (ci.valid) {
      doc["carbon"] = ci.nowGPerKwh;
      doc["carbonIndex"] = Carbon::indexName(ci.nowIndex);
    }
    if (t.fresh()) doc["rainRate"] = t.rainRateMmH;
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
  doc["board"] = board.name;
  doc["rssi"] = Net::rssi();
  doc["ip"] = Net::ip();
  doc["uptimeMin"] = millis() / 60000;
  doc["historyCount"] = History::count();
  doc["historyIntervalMin"] = HISTORY_INTERVAL_S / 60;
  doc["screenW"] = board.width;
  doc["screenH"] = board.height;
  JsonArray pages = doc["pages"].to<JsonArray>();
  for (uint8_t i = 0; i < board.pageCount; ++i) pages.add(board.pageName(i));
  if (board.extraJson) board.extraJson(doc);

  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

/** @brief Format "%.<prec>f" into @p buf, or leave it empty when the flag is clear. */
void field(char *buf, size_t n, bool have, float v, int prec) {
  if (have) snprintf(buf, n, "%.*f", prec, v);
  else buf[0] = '\0';
}

/** @brief GET /history.csv?days=N — stream the log as CSV (chunked). */
void handleHistory() {
  const int days = constrain(server.hasArg("days") ? server.arg("days").toInt() : HISTORY_DEFAULT_DAYS, 1, HISTORY_DAYS);
  const time_t now = time(nullptr);
  const uint32_t since = now > days * 86400L ? now - days * 86400L : 0;

  std::vector<HistoryRecord> recs;
  History::read(since, recs);  // Copy out first so the file lock isn't held while sending.

  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", "inline; filename=\"weather-history.csv\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv",
              "epoch,local_time,temp_c,feels_c,humidity_pct,pressure_hpa,rain_mm,wind_mph,gust_mph,wind_dir,"
              "weather_code,eaqi,pm25,battery_pct,battery_v,rssi_dbm,aircraft,aurora_nt,aurora_level,"
              "t_temp_c,t_humidity_pct,t_pressure_hpa,t_wind_mph,t_gust_mph,t_wind_dir,t_rain_mm,t_uv,t_solar_wm2,"
              "t_strikes,t_battery_v,t_rain_rate_mmh,carbon_g\n");

  String chunk;
  chunk.reserve(4096);
  char line[480], when[20];
  char f[19][12];
  for (const HistoryRecord &r : recs) {
    const time_t t = r.time;
    tm lt;
    localtime_r(&t, &lt);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &lt);
    const bool w = r.flags & HF_WEATHER, a = r.flags & HF_AIR, b = r.flags & HF_BATTERY, au = r.flags & HF_AURORA,
               te = r.flags & HF_TEMPEST;
    field(f[0], 12, w, r.temp10 / 10.0f, 1);
    field(f[1], 12, w, r.feels10 / 10.0f, 1);
    field(f[2], 12, w, r.humidity, 0);
    field(f[3], 12, w, r.pressure10 / 10.0f, 1);
    field(f[4], 12, w, r.rain100 / 100.0f, 2);
    field(f[5], 12, w, r.wind10 / 10.0f, 1);
    field(f[6], 12, w, r.gust10 / 10.0f, 1);
    field(f[7], 12, w, r.windDir, 0);
    field(f[8], 12, w, r.code, 0);
    field(f[9], 12, a, r.eaqi, 0);
    field(f[10], 12, a, r.pm25x10 / 10.0f, 1);
    field(f[11], 12, b, r.battPct, 0);
    field(f[12], 12, b, r.battMv / 1000.0f, 3);
    field(f[13], 12, au, r.aurora10 / 10.0f, 1);
    field(f[14], 12, au, r.auroraLevel, 0);
    char tf[13][12];
    field(tf[0], 12, te, r.tTemp10 / 10.0f, 1);
    field(tf[1], 12, te, r.tHumidity, 0);
    field(tf[2], 12, te, r.tPressure10 / 10.0f, 1);
    field(tf[3], 12, te, r.tWind10 / 10.0f, 1);
    field(tf[4], 12, te, r.tGust10 / 10.0f, 1);
    field(tf[5], 12, te, r.tWindDir, 0);
    field(tf[6], 12, te, r.tRain100 / 100.0f, 2);
    field(tf[7], 12, te, r.tUv10 / 10.0f, 1);
    field(tf[8], 12, te, r.tSolar, 0);
    field(tf[9], 12, te, r.tStrikes, 0);
    field(tf[10], 12, te, r.tBattMv / 1000.0f, 3);
    field(tf[11], 12, te, r.tRate10 / 10.0f, 1);
    field(tf[12], 12, r.flags & HF_CARBON, r.carbon, 0);
    snprintf(line, sizeof(line), "%lu,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%d,%u,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
             static_cast<unsigned long>(r.time), when, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10],
             f[11], f[12], r.rssi, r.aircraft, f[13], f[14], tf[0], tf[1], tf[2], tf[3], tf[4], tf[5], tf[6], tf[7], tf[8],
             tf[9], tf[10], tf[11], tf[12]);
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

void begin(const Board &b) {
  board = b;
  snapBuf = static_cast<uint16_t *>(heap_caps_malloc(board.width * board.height * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!snapBuf) {
    Log::println("[web] no memory for screenshot buffer");
    return;
  }

  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, [] { server.send(200, "text/html", WEB_PAGE); });
  server.on("/screen.bmp", HTTP_GET, handleScreen);
  Log::registerRoutes(server);
#if ENABLE_NET_TOOLS
  NetTest::registerRoutes(server);  // CrowPanel only: keeps the Feather image within its flash slot.
#endif
  server.on("/api/now.json", HTTP_GET, handleNow);
  server.on("/history.csv", HTTP_GET, handleHistory);
  WebSettings::registerRoutes(server);
  WebApi::registerRoutes(server, board.name);
  server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
  server.begin();

  xTaskCreatePinnedToCore(serverTask, "web", 8192, nullptr, 1, nullptr, 1);
  Log::printf("[web] http://%s.local/  (http://%s/)\n", HOSTNAME, Net::ip().c_str());
}

}  // namespace WebUi
