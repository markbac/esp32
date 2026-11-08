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
#if __has_include("secrets.h")
#include "secrets.h"  // Your Wi-Fi credentials (git-ignored).
#else
#include "secrets.example.h"  // Placeholder so a fresh clone builds; copy to secrets.h.
#endif
#include "status_led.h"
#include "ui.h"
#include "web_ui.h"

namespace {

Page page = Page::Now;          ///< Page currently displayed.
uint32_t lastBatteryRead = 0;   ///< millis() of last gauge read.
uint32_t lastPageChange = 0;    ///< millis() of last page change (auto or button).
uint32_t lastSerialLog = 0;     ///< millis() of last status line.
uint32_t lastRender = 0;        ///< millis() of last redraw.
uint32_t lastLed = 0;           ///< millis() of last LED update.
uint32_t lastHistorySlot = 0;   ///< Quarter-hour slot (epoch / interval) last logged.

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

/** @brief Clamp and convert to an integer field. */
template <typename T>
T scaled(float v, float scale, float lo, float hi) {
  return static_cast<T>(lroundf(constrain(v * scale, lo, hi)));
}

/** @brief Snapshot current data into a history record. */
HistoryRecord buildRecord(time_t now) {
  HistoryRecord r{};
  r.time = static_cast<uint32_t>(now);
  {
    DataService::Guard g;
    const WeatherData &wx = DataService::weather();
    if (wx.valid) {
      const CurrentWeather &c = wx.current;
      r.flags |= HF_WEATHER;
      r.temp10 = scaled<int16_t>(c.temp, 10, -32000, 32000);
      r.feels10 = scaled<int16_t>(c.feelsLike, 10, -32000, 32000);
      r.humidity = static_cast<uint8_t>(constrain(c.humidity, 0, 100));
      r.pressure10 = scaled<uint16_t>(c.pressure, 10, 0, 65535);
      r.rain100 = scaled<uint16_t>(c.precip, 100, 0, 65535);
      r.wind10 = scaled<uint16_t>(c.wind, 10, 0, 65535);
      r.gust10 = scaled<uint16_t>(c.gust, 10, 0, 65535);
      r.windDir = static_cast<uint16_t>(c.windDir);
      r.code = static_cast<uint8_t>(c.code);
    }
    const AirQualityData &aq = DataService::air();
    if (aq.valid) {
      r.flags |= HF_AIR;
      r.eaqi = static_cast<uint8_t>(constrain(aq.eaqi, 0, 255));
      r.pm25x10 = scaled<uint16_t>(aq.pm25, 10, 0, 65535);
    }
    const AuroraData &au = DataService::aurora();
    if (au.valid) {
      r.flags |= HF_AURORA;
      r.aurora10 = scaled<uint16_t>(Aurora::latestActivity(au), 10, 0, 65535);
      r.auroraLevel = static_cast<uint8_t>(au.level);
    }
    r.aircraft = static_cast<uint8_t>(min<uint16_t>(DataService::flights().total, 255));
  }
  const BatteryStatus &b = Battery::status();
  if (b.present) {
    r.flags |= HF_BATTERY;
    r.battPct = static_cast<uint8_t>(lroundf(b.percent));
    r.battMv = scaled<uint16_t>(b.voltage, 1000, 0, 65535);
  } else {
    r.battPct = 255;
  }
  r.rssi = static_cast<int8_t>(Net::rssi());
  return r;
}

/** @brief Append one record per quarter hour (aligned to :00, :15, :30, :45). */
void logHistory() {
  if (!Net::timeValid()) return;
  const time_t now = time(nullptr);
  const uint32_t slot = now / HISTORY_INTERVAL_S;
  if (slot == lastHistorySlot) return;
  {
    DataService::Guard g;
    if (!DataService::weather().valid) return;  // Wait for the first forecast after boot.
  }
  lastHistorySlot = slot;
  const HistoryRecord r = buildRecord(now);
  const bool ok = History::append(r);
  Serial.printf("[history] %s record %u (%.1fC, AQI %u, %u aircraft)\n", ok ? "logged" : "FAILED to log",
                History::count(), r.temp10 / 10.0f, r.eaqi, r.aircraft);
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
  History::begin();

  Net::begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CONNECT_TIMEOUT_MS);
  Net::beginTime(POSIX_TZ);
  // Give SNTP a moment so hourly data can be aligned to the current hour.
  for (int i = 0; i < 20 && !Net::timeValid(); ++i) delay(250);

  // Don't double-log a quarter hour that was recorded just before a reboot.
  lastHistorySlot = History::lastTime() / HISTORY_INTERVAL_S;

  DataService::begin();
  WebUi::begin();
  Battery::update();  // The gauge's first reading after power-up can be 0 V.
  lastPageChange = millis();
}

/** @brief UI loop on core 1; never blocks on the network. */
void loop() {
  pollButtons();

  if (PAGE_ROTATE_MS && due(lastPageChange, PAGE_ROTATE_MS)) page = nextAuto(page);
  if (due(lastBatteryRead, BATTERY_INTERVAL_MS)) Battery::update();
  if (due(lastLed, 50)) updateLed();
  if (due(lastSerialLog, SERIAL_LOG_INTERVAL_MS)) {
    logStatus();
    logHistory();
  }
  if (due(lastRender, RENDER_INTERVAL_MS)) render();

  delay(10);
}
