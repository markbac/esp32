/**
 * @file    main.cpp
 * @brief   Application entry point: start-up sequence and UI loop.
 *
 * @details Target: Adafruit Feather ESP32-S3 Reverse TFT (240 x 135 ST7789, LiPo gauge).
 *
 *          Work is split across the two CPU cores:
 *          - **Core 0** – DataService worker task downloads weather, air
 *            quality, warnings, aurora, flights and radar on their own schedules.
 *          - **Core 1** – this Arduino loop (buttons, page rotation, battery,
 *            alert LED, history logging, screen redraws) and the web server task.
 *          - Tempest UDP broadcasts are merged in by the AsyncUDP task.
 *            Neither waits on the network.
 *
 *          Buttons: D0 = next page, D1 = previous page, D2 = refresh all data.
 *
 * @author  Generated with Claude Code
 */
#include <Arduino.h>
#include <Wire.h>

#include "battery.h"
#include "config.h"
#include "connectivity.h"
#include "data_service.h"
#include "history.h"
#include "recorder.h"
#include "settings.h"
#if __has_include("secrets.h")
#include "secrets.h"  // Your Wi-Fi credentials (git-ignored).
#else
#include "secrets.example.h"  // Placeholder so a fresh clone builds; copy to secrets.h.
#endif
#include "status_led.h"
#include "tempest.h"
#include "ui.h"
#include "web_ui.h"

namespace {

Page page = Page::Now;          ///< Page currently displayed.
uint32_t lastBatteryRead = 0;   ///< millis() of last gauge read.
uint32_t lastPageChange = 0;    ///< millis() of last page change (auto or button).
uint32_t lastSerialLog = 0;     ///< millis() of last status line.
uint32_t lastRender = 0;        ///< millis() of last redraw.
uint32_t lastLed = 0;           ///< millis() of last LED update.

/**
 * @brief  True (and resets @p last) when @p interval ms have passed since @p last.
 * @param  last      Timestamp of the previous run, updated on true.
 * @param  interval  Period in milliseconds.
 */
bool due(uint32_t &last, uint32_t interval) {
  const uint32_t now = millis();
  if (now - last < interval) return false;
  last = now;
  return true;
}

/** @brief Edge-detecting, debounced push button. */
struct Button {
  int pin;              ///< GPIO number.
  bool activeLow;       ///< True if pressing pulls the pin low.
  bool wasPressed;      ///< Debounced state at the previous poll.
  uint32_t changedAt;   ///< millis() of the last accepted change.

  /** @brief Configure the pin with the matching pull resistor. */
  void begin() { pinMode(pin, activeLow ? INPUT_PULLUP : INPUT_PULLDOWN); }

  /** @brief True exactly once per press (30 ms debounce). */
  bool pressed() {
    const bool down = digitalRead(pin) == (activeLow ? LOW : HIGH);
    if (down == wasPressed || millis() - changedAt < 30) return false;
    wasPressed = down;
    changedAt = millis();
    return down;
  }
};

Button btnNext{BUTTON_NEXT_PIN, true, false, 0};
Button btnPrev{BUTTON_PREV_PIN, false, false, 0};
Button btnRefresh{BUTTON_REFRESH_PIN, false, false, 0};

/** @brief Build the read-only view the renderer uses. Caller holds the data lock. */
UiModel model() {
  return UiModel{DataService::weather(), DataService::air(),     DataService::warnings(), DataService::aurora(),
                 DataService::radar(),   DataService::flights(), Battery::status()};
}

/** @brief Next page for auto-rotation: skips Alerts when there's nothing to report. */
Page nextAuto(Page p) {
  p = Ui::next(p);
  if (p == Page::Alerts) {
    DataService::Guard g;
    const bool quiet = DataService::warnings().count == 0 && DataService::aurora().level < AuroraLevel::Amber;
    if (quiet) p = Ui::next(p);
  }
  return p;
}

/** @brief Change page and restart the auto-rotate timer. */
void showPage(Page p, const char *why) {
  page = p;
  lastPageChange = millis();
  lastRender = 0;  // Redraw immediately.
  Serial.printf("[ui] %s -> %s\n", why, Ui::pageName(page));
}

/** @brief Handle D0 (next page), D1 (previous page) and D2 (refresh everything). */
void pollButtons() {
  if (btnNext.pressed()) showPage(Ui::next(page), "D0");
  if (btnPrev.pressed()) showPage(Ui::prev(page), "D1");
  if (btnRefresh.pressed()) {
    Serial.println("[ui] D2 -> refresh all feeds");
    DataService::refreshAll();
  }
}

/** @brief Draw and push the current page (holds the lock so web snapshots can't interleave). */
void render() {
  DataService::Guard g;
  Ui::render(page, model());
}

/** @brief Update the NeoPixel from the current warning and aurora levels. */
void updateLed() {
  WarningLevel w;
  AuroraLevel a;
  {
    DataService::Guard g;
    w = DataService::warnings().highest();
    a = DataService::aurora().valid ? DataService::aurora().level : AuroraLevel::Unknown;
  }
  StatusLed::update(w, a);
}

/** @brief Web server hook: render @p p off-screen into @p out. */
void webSnapshot(uint8_t p, uint16_t *out) {
  DataService::Guard g;
  Ui::snapshot(static_cast<Page>(p), model(), out);
}

/** @brief Web server hook: page names. */
const char *webPageName(uint8_t p) { return Ui::pageName(static_cast<Page>(p)); }

/** @brief Web server hook: add the LiPo reading to /api/now.json. */
void webExtraJson(JsonDocument &doc) {
  const BatteryStatus &b = Battery::status();
  if (b.present) doc["battery"] = static_cast<int>(lroundf(b.percent));
}

/** @brief Log a history record when a new quarter hour starts. */
void logHistory() {
  const BatteryStatus &b = Battery::status();
  Recorder::DeviceBattery batt;
  batt.present = b.present;
  batt.percent = b.percent;
  batt.voltage = b.voltage;
  Recorder::poll(batt);
}

/** @brief One-line health summary for the serial monitor. */
void logStatus() {
  const BatteryStatus &b = Battery::status();
  DataService::Guard g;
  Serial.printf("[status] wifi=%s rssi=%d batt=%.2fV wx=%s air=%s warn=%u aurora=%s radar=%u planes=%u hist=%u "
                "heap=%lu psram=%lu\n",
                Net::connected() ? "up" : "down", Net::rssi(), b.voltage, DataService::weather().valid ? "ok" : "none",
                DataService::air().valid ? "ok" : "none", DataService::warnings().total,
                Aurora::levelName(DataService::aurora().level), DataService::radar().count, DataService::flights().total,
                History::count(), static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getFreePsram()));
}

}  // namespace

/** @brief Arduino start-up: display, LED, battery, storage, Wi-Fi, clock, data task, web server. */
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32-S3 TFT Weather Display ===");

  btnNext.begin();
  btnPrev.begin();
  btnRefresh.begin();

  Ui::begin();  // Also powers the I2C bus the gauge sits on.
  StatusLed::begin();
  Wire.begin();
  Battery::begin(Wire);
  Battery::update();
  Settings::begin();
  History::begin();

  // Wi-Fi from the web settings if set, falling back to the compiled-in network.
  const SettingsData cfg = Settings::get();
  bool wifiOk = false;
  if (cfg.wifiSsid.length()) wifiOk = Net::begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str(), WIFI_CONNECT_TIMEOUT_MS);
  if (!wifiOk) Net::begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CONNECT_TIMEOUT_MS);
  Net::beginTime(POSIX_TZ);
  // Give SNTP a moment so hourly data can be aligned to the current hour.
  for (int i = 0; i < 20 && !Net::timeValid(); ++i) delay(250);

  DataService::begin();
  Recorder::begin();
  Tempest::begin();
  WebUi::begin(WebUi::Board{"Feather ESP32-S3 Reverse TFT", Ui::WIDTH, Ui::HEIGHT,
                            static_cast<uint8_t>(Page::Count), webPageName, webSnapshot, webExtraJson});
  Battery::update();  // The gauge's first reading after power-up can be 0 V.
  lastPageChange = millis();
}

/** @brief UI loop on core 1; never blocks on the network. */
void loop() {
  pollButtons();

  if (PAGE_ROTATE_MS && due(lastPageChange, PAGE_ROTATE_MS)) page = nextAuto(page);
  if (due(lastBatteryRead, BATTERY_INTERVAL_MS)) {
    Battery::update();
    Net::maintain();
  }
  if (due(lastLed, 50)) updateLed();
  if (due(lastSerialLog, SERIAL_LOG_INTERVAL_MS)) {
    logStatus();
    logHistory();
  }
  if (due(lastRender, RENDER_INTERVAL_MS)) render();

  delay(10);
}
