/**
 * @file    ui.cpp
 * @brief   CrowPanel frame: status bar, tab bar, page dispatch, touch and snapshots.
 */
#include "ui.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "board_config.h"
#include "connectivity.h"
#include "ui_draw.h"

using namespace UiDraw;

namespace {

SemaphoreHandle_t canvasMutex = nullptr;  ///< Serialises display renders and web snapshots.

/** @brief RAII lock for the canvas. */
struct CanvasLock {
  CanvasLock() { xSemaphoreTake(canvasMutex, portMAX_DELAY); }
  ~CanvasLock() { xSemaphoreGive(canvasMutex); }
};

const char *const TAB_NAMES[] = {"Now", "Forecast", "Radar", "Air", "Grid", "Sky", "Alerts", "Flights", "History", "System"};
const char *const TITLES[] = {"Conditions now", "Forecast", "Rain radar", "Air quality", "Electricity grid",
                              "Sun & moon",     "Alerts",   "Flights overhead", "History & records", "System"};
constexpr int TABS = static_cast<int>(Page::Count);

/** @brief Clock, date, page title and status icons. */
void statusBar() {
  canvas.fillRect(0, 0, W, TOP_H, C::BAR);
  tm t;
  if (Net::localTime(t)) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    const int w = text(buf, 12, TOP_H / 2, F::M, C::TEXT, td::middle_left);
    strftime(buf, sizeof(buf), "%a %d %b", &t);
    text(buf, 12 + w + 12, TOP_H / 2 + 1, F::S, C::MUTED, td::middle_left);
  }
  text(TITLES[static_cast<int>(state.page)], W / 2, TOP_H / 2, F::M, C::TEXT, td::middle_center);

  // Right-hand icons, laid out right to left.
  int x = W - 12 - 24;
  wifiBars(x, 13, Net::bars(), Net::connected());
  x -= 12;

  const WarningList &wl = DataService::warnings();
  if (wl.count) {
    x -= 24;
    warningTriangle(x, 10, 24, warningColor(wl.highest()));
    x -= 10;
  }
  const FrostRisk fr = Frost::assess(DataService::weather(), DataService::tempest());
  if (fr.level >= FrostLevel::Moderate) {
    x -= textWidth("FROST", F::XS) + 20;
    chip(x, 8, "FROST", frostColor(fr.level), C::BG, F::XS);
    x -= 10;
  }
  const AuroraData &au = DataService::aurora();
  if (au.valid && au.level >= AuroraLevel::Amber) {
    x -= textWidth("AURORA", F::XS) + 20;
    chip(x, 8, "AURORA", auroraColor(au.level), C::BG, F::XS);
    x -= 10;
  }
  const TempestData &tp = DataService::tempest();
  const char *tl = tp.fresh() ? "Tempest" : "Tempest?";
  x -= textWidth(tl, F::XS) + 20;
  chip(x, 8, tl, tp.fresh() ? rgb(28, 80, 48) : C::CARD2, tp.fresh() ? C::GREEN : C::DIM,
       F::XS);
}

/** @brief Tab bar with the active tab highlighted and an alert dot on Alerts. */
void tabBar() {
  canvas.fillRect(0, TAB_Y, W, TAB_H, C::BAR);
  canvas.drawFastHLine(0, TAB_Y, W, C::LINE);
  const bool alert = DataService::warnings().count > 0 ||
                     (DataService::aurora().valid && DataService::aurora().level >= AuroraLevel::Amber) ||
                     Frost::assess(DataService::weather(), DataService::tempest()).level >= FrostLevel::Moderate;
  for (int i = 0; i < TABS; ++i) {
    const int x0 = i * W / TABS, x1 = (i + 1) * W / TABS;
    const bool active = i == static_cast<int>(state.page);
    if (active) {
      canvas.fillRect(x0, TAB_Y + 1, x1 - x0, TAB_H - 1, C::CARD2);
      canvas.fillRect(x0, TAB_Y, x1 - x0, 3, C::ACCENT);
    }
    const F tf = textWidth(TAB_NAMES[i], F::S) <= x1 - x0 - 6 ? F::S : F::XS;  // Shrink long labels.
    text(TAB_NAMES[i], (x0 + x1) / 2, TAB_Y + TAB_H / 2 + 1, tf, active ? C::ACCENT : C::MUTED, td::middle_center);
    if (i == static_cast<int>(Page::Alerts) && alert) canvas.fillCircle(x1 - 12, TAB_Y + 12, 5, C::ORANGE);
    if (i > 0) canvas.drawFastVLine(x0, TAB_Y + 10, TAB_H - 20, C::LINE);
  }
}

/** @brief Draw @p page completely. Caller holds the canvas and data locks. */
void compose(Page page) {
  const Page saved = state.page;
  state.page = page;
  canvas.fillScreen(C::BG);
  switch (page) {
    case Page::Now: pageNow(); break;
    case Page::Forecast: pageForecast(); break;
    case Page::Radar: pageRadar(); break;
    case Page::Air: pageAir(); break;
    case Page::Grid: pageGrid(); break;
    case Page::Sky: pageSky(); break;
    case Page::Alerts: pageAlerts(); break;
    case Page::Flights: pageFlights(); break;
    case Page::History: pageHistory(); break;
    case Page::System: pageSystem(); break;
    case Page::Count: break;
  }
  statusBar();
  tabBar();
  state.page = saved;
}

/** @brief Route a tap to the tab bar, status bar or the visible page. */
void tap(int x, int y) {
  if (y >= TAB_Y) {
    Ui::show(static_cast<Page>(constrain(x * TABS / W, 0, TABS - 1)));
    return;
  }
  if (y < TOP_H) {
    if (x > W / 2) Ui::show(Page::Alerts);  // Status icons lead to Alerts.
    return;
  }
  switch (state.page) {
    case Page::Radar: tapRadar(x, y); break;
    case Page::Flights: tapFlights(x, y); break;
    case Page::History: tapHistory(x, y); break;
    default: break;
  }
}

Ui::Stats stats_;  ///< Render timings and last tap, for diagnostics.

// Touch gesture state.
bool wasDown = false;
int startX = 0, startY = 0, lastX = 0, lastY = 0;

}  // namespace

namespace Ui {

bool begin() {
  canvasMutex = xSemaphoreCreateMutex();
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  if (!canvas.createSprite(W, H)) {
    Serial.println("[ui] sprite allocation failed");
    return false;
  }
  return true;
}

void render() {
  CanvasLock c;
  const uint32_t t0 = millis();
  {
    DataService::Guard g;
    compose(state.page);
  }
  const uint32_t t1 = millis();
  canvas.pushSprite(&Display::lcd(), 0, 0);
  stats_.composeMs = t1 - t0;
  stats_.pushMs = millis() - t1;
}

const Stats &stats() { return stats_; }

void handleTouch(bool down, int x, int y) {
  if (down && !wasDown) {
    startX = lastX = x;
    startY = lastY = y;
  } else if (down) {
    lastX = x;
    lastY = y;
  } else if (wasDown) {
    const int dx = lastX - startX, dy = lastY - startY;
    if (abs(dx) > CP_SWIPE_MIN_PX && abs(dx) > 2 * abs(dy) && startY < TAB_Y) {
      const int n = static_cast<int>(Page::Count);
      const int p = (static_cast<int>(state.page) + (dx < 0 ? 1 : n - 1)) % n;
      show(static_cast<Page>(p));
      Serial.printf("[touch] swipe %s\n", dx < 0 ? "left" : "right");
    } else if (abs(dx) < CP_TAP_MAX_MOVE_PX && abs(dy) < CP_TAP_MAX_MOVE_PX) {
      Serial.printf("[touch] tap %d,%d\n", startX, startY);
      stats_.lastTapX = startX;
      stats_.lastTapY = startY;
      stats_.lastTapAtMs = millis();
      tap(startX, startY);
    }
  }
  wasDown = down;
}

void snapshot(uint8_t page, uint16_t *out) {
  CanvasLock c;
  {
    DataService::Guard g;
    compose(static_cast<Page>(page));
  }
  // Average each 2x2 block; the sprite stores byte-swapped RGB565.
  const uint16_t *src = static_cast<const uint16_t *>(canvas.getBuffer());
  auto px = [&](int x, int y) {
    const uint16_t v = src[y * W + x];
    return static_cast<uint16_t>((v >> 8) | (v << 8));
  };
  for (int y = 0; y < WEB_H; ++y) {
    for (int x = 0; x < WEB_W; ++x) {
      uint32_t r = 0, g = 0, b = 0;
      for (int k = 0; k < 4; ++k) {
        const uint16_t c = px(x * 2 + (k & 1), y * 2 + (k >> 1));
        r += c >> 11;
        g += (c >> 5) & 0x3F;
        b += c & 0x1F;
      }
      out[y * WEB_W + x] = static_cast<uint16_t>(((r / 4) << 11) | ((g / 4) << 5) | (b / 4));
    }
  }
}

Page current() { return state.page; }

void show(Page p) {
  if (p == state.page) return;
  state.page = p;
  Serial.printf("[ui] page %s\n", pageName(p));
}

const char *pageName(Page p) {
  const int i = static_cast<int>(p);
  return i < TABS ? TAB_NAMES[i] : "?";
}

}  // namespace Ui
