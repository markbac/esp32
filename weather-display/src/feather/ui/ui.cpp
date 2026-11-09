/**
 * @file    ui.cpp
 * @brief   Display set-up, frame composition, status bar and shared drawing helpers.
 */
#include "ui.h"

#include <Adafruit_ST7789.h>

#include "connectivity.h"
#include "theme.h"
#include "ui_draw.h"

namespace UiDraw {

GFXcanvas16 canvas(W, H);
const char *const WEEKDAYS[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

void text(const char *s, int x, int y, uint8_t size, uint16_t color) {
  canvas.setTextSize(size);
  canvas.setTextColor(color);
  canvas.setCursor(x, y);
  canvas.print(s);
}

void centered(const char *s, int cx, int y, uint8_t size, uint16_t color) {
  text(s, cx - static_cast<int>(strlen(s)) * CHAR_W * size / 2, y, size, color);
}

void rightAligned(const char *s, int right, int y, uint8_t size, uint16_t color) {
  text(s, right - static_cast<int>(strlen(s)) * CHAR_W * size, y, size, color);
}

void degree(int x, int y, int r, uint16_t color) {
  canvas.drawCircle(x + r, y + r, r, color);
  if (r > 2) canvas.drawCircle(x + r, y + r, r - 1, color);
}

void tempCentered(float t, int cx, int y, uint8_t size, uint16_t color) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", static_cast<int>(lroundf(t)));
  const int ring = size + 1;
  const int numW = strlen(buf) * CHAR_W * size;
  const int x = cx - (numW + 2 * ring + 1) / 2;
  text(buf, x, y, size, color);
  degree(x + numW, y, ring, color);
}

void waiting(const char *what) {
  centered(Net::connected() ? what : "Connecting Wi-Fi...", W / 2, 64, 1, Theme::LIGHT);
}

uint16_t warningColor(WarningLevel l) {
  switch (l) {
    case WarningLevel::Red: return Theme::RED;
    case WarningLevel::Amber: return Theme::ORANGE;
    default: return Theme::YELLOW;
  }
}

void warningIcon(int x, int y, WarningLevel l) {
  canvas.fillTriangle(x + 6, y, x, y + 11, x + 12, y + 11, warningColor(l));
  canvas.drawFastVLine(x + 6, y + 4, 4, Theme::BG);
  canvas.drawPixel(x + 6, y + 9, Theme::BG);
}

uint16_t auroraColor(AuroraLevel l) {
  switch (l) {
    case AuroraLevel::Red: return Theme::RED;
    case AuroraLevel::Amber: return Theme::ORANGE;
    case AuroraLevel::Yellow: return Theme::YELLOW;
    case AuroraLevel::Green: return Theme::GREEN;
    default: return Theme::GREY;
  }
}

void auroraIcon(int x, int y, AuroraLevel l) {
  // Three wavy "curtain" strands, green at the base, level colour at the top.
  for (int i = 0; i < 3; ++i) {
    const int bx = x + 2 + i * 4;
    canvas.drawLine(bx, y + 11, bx + 1, y + 6, Theme::GREEN);
    canvas.drawLine(bx + 1, y + 6, bx, y + 1, auroraColor(l));
  }
  canvas.drawFastHLine(x, y + 11, 13, Theme::DKGREY);
}

void localHHMM(time_t t, char *buf) {
  tm lt;
  localtime_r(&t, &lt);
  snprintf(buf, 6, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

}  // namespace UiDraw

namespace {

using namespace UiDraw;

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);  ///< Physical display.

/** @brief Wi-Fi strength as four bars, or a red cross when offline (16 x 12). */
void wifiIcon(int x, int y) {
  if (!Net::connected()) {
    canvas.drawLine(x + 2, y + 1, x + 12, y + 11, Theme::RED);
    canvas.drawLine(x + 12, y + 1, x + 2, y + 11, Theme::RED);
    return;
  }
  const int bars = Net::bars();
  for (int i = 0; i < 4; ++i) {
    const int h = 3 + i * 3;
    canvas.fillRect(x + i * 4, y + 12 - h, 3, h, i < bars ? Theme::WHITE : Theme::DKGREY);
  }
}

/** @brief Battery outline with fill level, charge bolt, or "USB" (24 x 11). */
void batteryIcon(int x, int y, const BatteryStatus &b) {
  canvas.drawRect(x, y, 22, 11, Theme::WHITE);
  canvas.fillRect(x + 22, y + 3, 2, 5, Theme::WHITE);
  if (!b.present) {
    text("USB", x + 3, y + 2, 1, Theme::CYAN);
    return;
  }
  const uint16_t fill = b.percent > 50 ? Theme::GREEN : b.percent > 20 ? Theme::YELLOW : Theme::RED;
  canvas.fillRect(x + 2, y + 2, lroundf(18 * b.percent / 100.0f), 7, fill);
  if (b.charging()) {
    canvas.fillTriangle(x + 12, y + 1, x + 7, y + 6, x + 11, y + 6, Theme::WHITE);
    canvas.fillTriangle(x + 10, y + 5, x + 15, y + 5, x + 10, y + 10, Theme::WHITE);
  }
}

/** @brief Clock, date, warning, Wi-Fi bars and battery across the top of the screen. */
void statusBar(const UiModel &m) {
  canvas.fillRect(0, 0, W, BAR_H, Theme::BAR);
  tm t;
  if (Net::localTime(t)) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    text(buf, 4, 2, 2, Theme::WHITE);
    strftime(buf, sizeof(buf), "%a %d", &t);
    text(buf, 68, 6, 1, Theme::LIGHT);
  } else {
    text("--:--", 4, 2, 2, Theme::WHITE);
  }

  // Alert icons sit between the date (ends x~104) and the Wi-Fi bars (x 150).
  if (m.aurora.valid && m.aurora.level >= AuroraLevel::Amber) auroraIcon(113, 3, m.aurora.level);
  if (m.warnings.count) warningIcon(132, 3, m.warnings.highest());
  wifiIcon(150, 3);
  if (m.battery.present) {
    char pct[6];
    snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(lroundf(m.battery.percent)));
    rightAligned(pct, 210, 6, 1, Theme::WHITE);
  }
  batteryIcon(213, 4, m.battery);
}

/** @brief Dots along the bottom showing which page is visible. */
void pageDots(Page page) {
  const int n = static_cast<int>(Page::Count);
  const int x0 = W / 2 - (n - 1) * 4;
  for (int i = 0; i < n; ++i) {
    canvas.fillCircle(x0 + i * 8, H - 4, 2, i == static_cast<int>(page) ? Theme::WHITE : Theme::DKGREY);
  }
}

}  // namespace

namespace Ui {

void begin() {
  // One GPIO powers both the TFT and the STEMMA/I2C bus (fuel gauge).
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
  delay(10);

  tft.init(135, 240);
  tft.setRotation(3);  // Landscape.
  tft.fillScreen(Theme::BG);
  canvas.cp437(true);
}

/** @brief Draw @p page (status bar, body, dots) into the canvas (internal). */
static void compose(Page page, const UiModel &m) {
  canvas.fillScreen(Theme::BG);
  statusBar(m);

  switch (page) {
    case Page::Now: pageNow(m); break;
    case Page::Hourly: pageHourly(m); break;
    case Page::Daily: pageDaily(m); break;
    case Page::Radar: pageRadar(m); break;
    case Page::Air: pageAir(m); break;
    case Page::Alerts: pageAlerts(m); break;
    case Page::Sky: pageSky(m); break;
    case Page::Flights: pageFlights(m); break;
    case Page::System: pageSystem(m); break;
    case Page::Count: break;
  }
  pageDots(page);
}

void render(Page page, const UiModel &m) {
  compose(page, m);
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), W, H);
}

void snapshot(Page page, const UiModel &m, uint16_t *out) {
  compose(page, m);
  memcpy(out, canvas.getBuffer(), W * H * sizeof(uint16_t));
}

const char *pageName(Page p) {
  static const char *const names[] = {"Now", "Hourly", "5-Day", "Radar", "Air", "Alerts", "Sky", "Flights", "System"};
  static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(Page::Count), "one name per page");
  const uint8_t i = static_cast<uint8_t>(p);
  return i < static_cast<uint8_t>(Page::Count) ? names[i] : "?";
}

Page prev(Page p) {
  const uint8_t n = static_cast<uint8_t>(Page::Count);
  return static_cast<Page>((static_cast<uint8_t>(p) + n - 1) % n);
}

Page next(Page p) {
  return static_cast<Page>((static_cast<uint8_t>(p) + 1) % static_cast<uint8_t>(Page::Count));
}

}  // namespace Ui
