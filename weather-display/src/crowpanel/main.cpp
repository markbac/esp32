/**
 * @file    main.cpp
 * @brief   CrowPanel 7" weather station: start-up and UI loop.
 *
 * @details Target: Elecrow CrowPanel 7.0" Basic (ESP32-S3, 800x480 RGB, GT911 touch).
 *
 *          - **Core 0** – DataService worker downloads forecast, air quality,
 *            warnings, aurora, flights (+ on-demand routes) and radar.
 *          - **AsyncUDP task** – merges WeatherFlow Tempest broadcasts.
 *          - **Core 1** – this loop: touch polling, rendering, night
 *            backlight, sleep mode, auto paging, Wi-Fi survey, history
 *            logging; plus the web server task.
 *
 * @author  Generated with Claude Code
 */
#include <Arduino.h>
#include "log.h"

#include "astro.h"
#include "board_config.h"
#include "config.h"
#include "connectivity.h"
#include "data_service.h"
#include "display.h"
#include "history.h"
#include "net_test.h"
#include "recorder.h"
#include "settings.h"
#if __has_include("secrets.h")
#include "secrets.h"  // Your Wi-Fi credentials (git-ignored).
#else
#include "secrets.example.h"  // Placeholder so a fresh clone builds; copy to secrets.h.
#endif
#include "tempest.h"
#include "ui.h"
#include "web_settings.h"
#include "web_ui.h"
#include "wifi_scan.h"

namespace {

uint32_t lastRender = 0;      ///< millis() of last redraw.
uint32_t lastTouch = 0;       ///< millis() of last touch poll.
uint32_t lastSlow = 0;        ///< millis() of last once-a-second housekeeping.
uint32_t lastSerialLog = 0;   ///< millis() of last status line.
uint32_t lastLogFlush = 0;    ///< millis() of last SD log write.
uint8_t backlight = 0;        ///< Current backlight level.
bool asleep = false;          ///< Screen off for sleep mode.
uint32_t wakeUntilMs = 0;     ///< A touch during sleep hours keeps the screen on until this.
uint32_t lastPageChangeMs = 0;  ///< For auto paging.
Page lastPage = Page::Now;

/** @brief True (and resets @p last) when @p interval ms have passed. */
bool due(uint32_t &last, uint32_t interval) {
  const uint32_t now = millis();
  if (now - last < interval) return false;
  last = now;
  return true;
}

/** @brief True while sleep mode should keep the screen off. */
bool sleepDue(const SettingsData &cfg) {
  tm t;
  if (!cfg.sleepEnabled || !Net::localTime(t)) return false;
  if (static_cast<int32_t>(wakeUntilMs - millis()) > 0) return false;  // Woken by a touch.
  return Settings::inSleepWindow(cfg, t.tm_hour * 60 + t.tm_min);
}

/** @brief Sleep mode, or dim the backlight from one hour after sunset until sunrise. */
void updateBacklight() {
  const SettingsData cfg = Settings::get();
  const bool sleepNow = sleepDue(cfg);
  if (sleepNow != asleep) {
    asleep = sleepNow;
    Log::printf("[power] screen %s\n", asleep ? "asleep" : "awake");
    if (!asleep) lastRender = millis() - 100000;  // Fresh frame before the light comes on.
  }
  uint8_t want = asleep ? 0 : cfg.backlightDay;
  tm t;
  if (!asleep && cfg.backlightNight && Net::localTime(t)) {
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

/** @brief Move to the next page when auto paging is on and nobody has touched the screen recently. */
void autoPage() {
  const uint32_t now = millis();
  if (Ui::current() != lastPage) {  // Any page change (tap, swipe or auto) restarts the timer.
    lastPage = Ui::current();
    lastPageChangeMs = now;
  }
  const SettingsData cfg = Settings::get();
  if (!cfg.autoPage || asleep) return;
  if (Ui::current() == Page::Settings) return;                      // Don't move away while editing.
  if (NetTest::running() || NetTest::monitoring()) return;          // Keep a Wi-Fi test on screen.
  if (Ui::stats().lastTouchMs && now - Ui::stats().lastTouchMs < CP_TOUCH_HOLD_MS) return;
  if (now - lastPageChangeMs < cfg.autoPageSecs * 1000UL) return;
  Ui::advance();
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
  doc["asleep"] = asleep;
  const NetTestResult t = NetTest::get();
  if (t.state == NetTestResult::State::Done) {
    JsonObject n = doc["netTest"].to<JsonObject>();
    n["verdict"] = t.verdict();
    n["rssi"] = t.rssi;
    n["routerMs"] = t.router.avgMs;
    n["routerJitterMs"] = t.router.jitterMs;
    n["routerLossPct"] = t.router.lossPct();
    n["internetMs"] = t.internet.avgMs;
    n["internetJitterMs"] = t.internet.jitterMs;
    n["internetLossPct"] = t.internet.lossPct();
    n["downMbps"] = t.downMbps;
    n["agoS"] = (millis() - t.finishedAtMs) / 1000;
  }
  static MonitorData m;  // 3 KB: keep it off the web task's stack.
  NetTest::monitor(m);
  if (m.running || m.count) {
    JsonObject o = doc["monitor"].to<JsonObject>();
    o["running"] = m.running;
    o["verdict"] = m.verdict();
    o["minutes"] = m.count;
    o["durationMin"] = m.durationMin;
    o["routerLost"] = m.routerLost;
    o["routerSent"] = m.routerSent;
    o["internetLost"] = m.inetLost;
    o["internetSent"] = m.inetSent;
    o["spikes"] = m.spikes;
    o["dropouts"] = m.dropouts;
    o["worstMs"] = m.worstMs;
    o["weakestRssi"] = m.rssiMin;
  }
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
  Log::printf("[status] wifi=%s rssi=%d tempest=%s %.1fC wx=%s air=%s warn=%u aurora=%s radar=%u planes=%u hist=%u "
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
  Log::println("\n=== CrowPanel 7\" Weather Station ===");
  Log::printf("PSRAM %lu KB, flash %lu KB\n", static_cast<unsigned long>(ESP.getPsramSize() / 1024),
                static_cast<unsigned long>(ESP.getFlashChipSize() / 1024));

  if (!Display::begin() || !Ui::begin()) Log::println("[setup] display problem - continuing headless");
  Display::lcd().setFont(&fonts::DejaVu24);
  Display::lcd().setTextColor(TFT_WHITE);
  Display::lcd().drawString("Connecting to Wi-Fi...", 40, 40);

  Log::begin();  // SD card logs (buffered until the clock is set, then /logs/*-YYYY-MM-DD.*).
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
  WebSettings::setPages(static_cast<uint8_t>(Page::Count), webPageName,
                        [](uint8_t p) { return Ui::rotatable(static_cast<Page>(p)); });
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
    // In sleep hours a touch keeps the screen on for CP_WAKE_MS; the touch that
    // wakes it is swallowed until the finger lifts, so it can't press anything.
    static bool swallow = false;
    if (down && (asleep || static_cast<int32_t>(wakeUntilMs - millis()) > 0)) wakeUntilMs = millis() + CP_WAKE_MS;
    if (asleep && down) {
      swallow = true;
      updateBacklight();
    }
    if (swallow) {
      if (!down) swallow = false;
      return;
    }
    Ui::handleTouch(down, x, y);
    // Redraw at once after a page change or any tap (selection, pause, range...).
    if (Ui::current() != before || Ui::stats().lastTapAtMs != tapsBefore) lastRender = millis() - 100000;
  }
  // A frame costs ~150 ms to draw + ~50 ms to push, so only animate where it matters.
  const uint32_t period = Ui::current() == Page::Radar ? RADAR_FRAME_MS : CP_RENDER_MS;
  if (!asleep && due(lastRender, period)) Ui::render();
  if (due(lastSlow, 1000)) {
    updateBacklight();
    autoPage();
    Net::maintain();
    WifiScan::poll();
    Settings::flush();
    Recorder::poll();
  }
  if (due(lastSerialLog, SERIAL_LOG_INTERVAL_MS)) logStatus();
  if (due(lastLogFlush, 5000)) Log::flush();
  delay(2);
}
