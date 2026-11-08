/**
 * @file    main.cpp
 * @brief   CrowPanel 7" weather station: start-up and UI loop.
 *
 * @details Target: Elecrow CrowPanel 7.0" Basic (ESP32-S3, 800x480 RGB, GT911 touch).
 *
 *          - **Core 0** – DataService worker downloads forecast, air quality,
 *            warnings, aurora, flights (+ on-demand routes) and radar.
 *          - **AsyncUDP task** – merges WeatherFlow Tempest broadcasts.
 *          - **Core 1** – this loop: touch polling, rendering (4 fps), night
 *            backlight, history logging; plus the web server task.
 *
 * @author  Generated with Claude Code
 */
#include <Arduino.h>

#include "astro.h"
#include "board_config.h"
#include "config.h"
#include "connectivity.h"
#include "data_service.h"
#include "display.h"
#include "history.h"
#include "recorder.h"
#include "settings.h"
#if __has_include("secrets.h")
#include "secrets.h"  // Your Wi-Fi credentials (git-ignored).
#else
#include "secrets.example.h"  // Placeholder so a fresh clone builds; copy to secrets.h.
#endif
#include "tempest.h"
#include "ui.h"
#include "web_ui.h"

namespace {

uint32_t lastRender = 0;      ///< millis() of last redraw.
uint32_t lastTouch = 0;       ///< millis() of last touch poll.
uint32_t lastSlow = 0;        ///< millis() of last once-a-second housekeeping.
uint32_t lastSerialLog = 0;   ///< millis() of last status line.
uint8_t backlight = 0;        ///< Current backlight level.

/** @brief True (and resets @p last) when @p interval ms have passed. */
bool due(uint32_t &last, uint32_t interval) {
  const uint32_t now = millis();
  if (now - last < interval) return false;
  last = now;
  return true;
}

/** @brief Dim the backlight from one hour after sunset until sunrise. */
void updateBacklight() {
  const SettingsData cfg = Settings::get();
  uint8_t want = cfg.backlightDay;
  tm t;
  if (cfg.backlightNight && Net::localTime(t)) {
    int rise = -1, set = -1;
    {
      DataService::Guard g;
      rise = Astro::minutesOfDay(DataService::weather().sunrise);
      set = Astro::minutesOfDay(DataService::weather().sunset);
    }
    const int now = t.tm_hour * 60 + t.tm_min;
    if (rise >= 0 && set >= 0 && (now < rise || now > set + 60)) want = cfg.backlightNight;
  }
  if (want != backlight) {
    backlight = want;
    Display::setBrightness(want);
  }
}

/** @brief Web hooks. */
const char *webPageName(uint8_t p) { return Ui::pageName(static_cast<Page>(p)); }
void webSnapshot(uint8_t p, uint16_t *out) { Ui::snapshot(p, out); }

/** @brief Extra diagnostics in /api/now.json: render timing, current page, last tap. */
void webExtraJson(JsonDocument &doc) {
  const Ui::Stats &s = Ui::stats();
  doc["page"] = Ui::pageName(Ui::current());
  doc["composeMs"] = s.composeMs;
  doc["pushMs"] = s.pushMs;
  if (s.lastTapX >= 0) {
    doc["lastTap"]["x"] = s.lastTapX;
    doc["lastTap"]["y"] = s.lastTapY;
    doc["lastTap"]["agoS"] = (millis() - s.lastTapAtMs) / 1000;
  }
}

/** @brief One-line health summary for the serial monitor. */
void logStatus() {
  DataService::Guard g;
  const TempestData &t = DataService::tempest();
  Serial.printf("[status] wifi=%s rssi=%d tempest=%s %.1fC wx=%s air=%s warn=%u aurora=%s radar=%u planes=%u hist=%u "
                "heap=%u psram=%u\n",
                Net::connected() ? "up" : "down", Net::rssi(), t.fresh() ? "live" : "none", t.obs.tempC,
                DataService::weather().valid ? "ok" : "none", DataService::air().valid ? "ok" : "none",
                DataService::warnings().total, Aurora::levelName(DataService::aurora().level), DataService::radar().count,
                DataService::flights().total, History::count(), heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

}  // namespace

/** @brief Start-up: display, storage, Wi-Fi, clock, data task, Tempest listener, web server. */
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== CrowPanel 7\" Weather Station ===");
  Serial.printf("PSRAM %lu KB, flash %lu KB\n", static_cast<unsigned long>(ESP.getPsramSize() / 1024),
                static_cast<unsigned long>(ESP.getFlashChipSize() / 1024));

  if (!Display::begin() || !Ui::begin()) Serial.println("[setup] display problem - continuing headless");
  Display::lcd().setFont(&fonts::DejaVu24);
  Display::lcd().setTextColor(TFT_WHITE);
  Display::lcd().drawString("Connecting to Wi-Fi...", 40, 40);

  Settings::begin();
  History::begin();
  // Wi-Fi from the web settings if set, falling back to the compiled-in network.
  const SettingsData cfg = Settings::get();
  bool wifiOk = false;
  if (cfg.wifiSsid.length()) wifiOk = Net::begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str(), WIFI_CONNECT_TIMEOUT_MS);
  if (!wifiOk) Net::begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CONNECT_TIMEOUT_MS);
  Net::beginTime(POSIX_TZ);
  for (int i = 0; i < 20 && !Net::timeValid(); ++i) delay(250);

  DataService::begin();
  Recorder::begin();
  Tempest::begin();
  WebUi::begin(WebUi::Board{"CrowPanel 7\"", Ui::WEB_W, Ui::WEB_H, static_cast<uint8_t>(Page::Count), webPageName,
                            webSnapshot, webExtraJson});
  updateBacklight();
}

/** @brief UI loop on core 1. */
void loop() {
  if (due(lastTouch, CP_TOUCH_POLL_MS)) {
    int x = 0, y = 0;
    const bool down = Display::touch(x, y);
    const uint32_t tapsBefore = Ui::stats().lastTapAtMs;
    const Page before = Ui::current();
    Ui::handleTouch(down, x, y);
    // Redraw at once after a page change or any tap (selection, pause, range...).
    if (Ui::current() != before || Ui::stats().lastTapAtMs != tapsBefore) lastRender = millis() - 100000;
  }
  // A frame costs ~150 ms to draw + ~50 ms to push, so only animate where it matters.
  const uint32_t period = Ui::current() == Page::Radar ? RADAR_FRAME_MS : CP_RENDER_MS;
  if (due(lastRender, period)) Ui::render();
  if (due(lastSlow, 1000)) {
    updateBacklight();
    Recorder::poll();
  }
  if (due(lastSerialLog, SERIAL_LOG_INTERVAL_MS)) logStatus();
  delay(2);
}
