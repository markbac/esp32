/**
 * @file    page_system.cpp
 * @brief   "System" page: network, Tempest hub/sensor, device health and data feed ages.
 */
#include <LittleFS.h>
#include <esp_heap_caps.h>

#include "board_config.h"
#include "config.h"
#include "connectivity.h"
#include "history.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

/** @brief Label on the left, value right-aligned, one row of a card. */
void kv(int x, int w, int y, const char *label, const char *value, uint16_t col = C::TEXT) {
  text(label, x + 16, y, F::XS, C::MUTED);
  text(value, x + w - 16, y, F::XS, col, td::top_right);
}

void uptime(uint32_t s, char *buf, size_t n) {
  if (s >= 86400) snprintf(buf, n, "%lud %luh", static_cast<unsigned long>(s / 86400), static_cast<unsigned long>(s % 86400 / 3600));
  else snprintf(buf, n, "%luh %02lum", static_cast<unsigned long>(s / 3600), static_cast<unsigned long>(s % 3600 / 60));
}

}  // namespace

void pageSystem() {
  char buf[48];
  constexpr int cw = (W - 3 * M) / 2, ch = (BODY_H - 3 * M) / 2;
  constexpr int x0 = M, x1 = 2 * M + cw, y0 = BODY_Y + M, y1 = BODY_Y + 2 * M + ch;
  constexpr int step = 22;

  // Network.
  card(x0, y0, cw, ch, "Network");
  int y = y0 + 40;
  kv(x0, cw, y, "Wi-Fi", Net::connected() ? Net::ssid().c_str() : "offline", Net::connected() ? C::TEXT : C::RED);
  y += step;
  snprintf(buf, sizeof(buf), "%d dBm (%d%%)", Net::rssi(), Net::quality());
  kv(x0, cw, y, "Signal", buf);
  y += step;
  kv(x0, cw, y, "IP address", Net::ip().c_str());
  y += step;
  kv(x0, cw, y, "Web dashboard", "http://" HOSTNAME ".local", C::ACCENT);
  y += step;
  ago(DataService::flights().fetchedAtMs, buf, sizeof(buf));
  kv(x0, cw, y, "Last flight poll", buf);

  // Tempest.
  const TempestData &t = DataService::tempest();
  card(x1, y0, cw, ch, "Tempest station");
  y = y0 + 40;
  if (!t.packets) {
    text("No broadcasts heard on UDP 50222", x1 + 16, y, F::XS, C::DIM);
  } else {
    snprintf(buf, sizeof(buf), "%s  fw %u  (%s)", t.hubSerial, t.hubFirmware, t.hubIp);
    kv(x1, cw, y, "Hub", buf);
    y += step;
    snprintf(buf, sizeof(buf), "Wi-Fi %d dBm, up ", t.hubWifiRssi);
    uptime(t.hubUptime, buf + strlen(buf), sizeof(buf) - strlen(buf));
    kv(x1, cw, y, "Hub status", buf);
    y += step;
    snprintf(buf, sizeof(buf), "%s  fw %u", t.sensorSerial, t.sensorFirmware);
    kv(x1, cw, y, "Sensor", buf);
    y += step;
    snprintf(buf, sizeof(buf), "%.2f V, radio %d dBm", t.sensorVoltage, t.sensorRssi);
    kv(x1, cw, y, "Sensor battery", buf, t.sensorVoltage > 2.39f ? C::GREEN : C::ORANGE);
    y += step;
    ago(t.obsAtMs, buf, sizeof(buf));
    kv(x1, cw, y, "Last observation", buf, t.fresh() ? C::TEXT : C::ORANGE);
    y += step;
    snprintf(buf, sizeof(buf), "%s, %lu packets", t.sensorStatus ? "FAULT" : "OK", static_cast<unsigned long>(t.packets));
    kv(x1, cw, y, "Sensor status", buf, t.sensorStatus ? C::RED : C::GREEN);
  }

  // Device.
  card(x0, y1, cw, ch, "Display");
  y = y1 + 40;
  uptime(millis() / 1000, buf, sizeof(buf));
  kv(x0, cw, y, "Uptime", buf);
  y += step;
  snprintf(buf, sizeof(buf), "%u KB free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
  kv(x0, cw, y, "RAM", buf);
  y += step;
  snprintf(buf, sizeof(buf), "%u KB free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
  kv(x0, cw, y, "PSRAM", buf);
  y += step;
  snprintf(buf, sizeof(buf), "%u records, %u/%u KB", History::count(), LittleFS.usedBytes() / 1024,
           LittleFS.totalBytes() / 1024);
  kv(x0, cw, y, "History", buf);
  y += step;
  snprintf(buf, sizeof(buf), "GT911 0x%02X%s, pclk %lu MHz", Display::touchAddress(), Display::hasExpander() ? " +PCA9557" : "",
           static_cast<unsigned long>(CP_PCLK_HZ / 1000000));
  kv(x0, cw, y, "Hardware", buf);

  // Feeds.
  card(x1, y1, cw, ch, "Data feeds");
  DataService::FeedAge feeds[8];
  const uint8_t n = DataService::feedStatus(feeds, 8);
  y = y1 + 40;
  for (uint8_t i = 0; i < n; ++i) {
    ago(feeds[i].okAtMs, buf, sizeof(buf));
    kv(x1, cw, y, feeds[i].name, buf, feeds[i].lastOk ? C::TEXT : C::ORANGE);
    y += step;
  }
}

}  // namespace UiDraw
