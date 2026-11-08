/**
 * @file    page_system.cpp
 * @brief   Wi-Fi, battery, memory and uptime diagnostics.
 */
#include <esp_heap_caps.h>

#include "connectivity.h"
#include "theme.h"
#include "ui_draw.h"

namespace UiDraw {

void pageSystem(const UiModel &m) {
  const BatteryStatus &b = m.battery;
  char buf[64];
  int y = 22;
  auto line = [&](const char *s, uint16_t c) { text(s, 4, y, 2, c); y += 18; };

  if (Net::connected()) {
    snprintf(buf, sizeof(buf), "WiFi %ddBm %d%%", Net::rssi(), Net::quality());
    line(buf, Theme::WHITE);
    snprintf(buf, sizeof(buf), "%.19s", Net::ssid().c_str());
    line(buf, Theme::LIGHT);
    line(Net::ip().c_str(), Theme::LIGHT);
  } else {
    line("WiFi offline", Theme::RED);
    y += 36;
  }

  if (b.present) {
    snprintf(buf, sizeof(buf), "Batt %d%% %.2fV", static_cast<int>(lroundf(b.percent)), b.voltage);
    line(buf, b.percent > 20 ? Theme::GREEN : Theme::RED);
    if (b.rateSupported) snprintf(buf, sizeof(buf), "%s %+.1f%%/h", b.charging() ? "Charging" : "Draining", b.chargeRate);
    else snprintf(buf, sizeof(buf), "On battery");
    line(buf, Theme::LIGHT);
  } else {
    line(b.gaugeFound ? "USB power" : "No batt gauge", Theme::CYAN);
    line(b.gaugeFound ? "(no battery)" : "", Theme::GREY);
  }

  const uint32_t upMin = millis() / 60000;
  snprintf(buf, sizeof(buf), "Up %luh%02lum  RAM %uK  PSRAM %uK", static_cast<unsigned long>(upMin / 60),
           static_cast<unsigned long>(upMin % 60), heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
  text(buf, 4, y + 2, 1, Theme::GREY);
}

}  // namespace UiDraw
