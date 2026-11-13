/**
 * @file    ui.cpp
 * @brief   CrowPanel frame: status bar, tab bar, page dispatch, touch and snapshots.
 */
#include "ui.h"
#include "log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "board_config.h"
#include "connectivity.h"
#include "settings.h"
#include "ui_draw.h"

using namespace UiDraw;

namespace {

SemaphoreHandle_t canvasMutex = nullptr;  ///< Serialises display renders and web snapshots.

/** @brief RAII lock for the canvas. */
struct CanvasLock {
  CanvasLock() { xSemaphoreTake(canvasMutex, portMAX_DELAY); }
  ~CanvasLock() { xSemaphoreGive(canvasMutex); }
};

/** @brief Sub-tab label (top bar) for each page. */
const char *const PAGE_NAMES[] = {"Now",    "Forecast", "Radar",  "Alerts", "Air quality", "Grid",
                                  "Sky",    "Flights",  "History", "Home",  "Lab",         "Signal",
                                  "Crowding", "Test",   "Monitor",  "Help",   "Status", "Settings"};
/** @brief Title shown when a group has a single page. */
const char *const TITLES[] = {"Conditions now", "Forecast", "Rain radar", "Alerts", "Air quality", "Electricity grid",
                              "Sun & moon", "Flights overhead", "History & records", "Home Assistant", "Home lab",
                              "Wi-Fi signal", "Wi-Fi crowding", "Network test", "Network monitor", "Network help", "System", "Settings"};
constexpr int PAGES = static_cast<int>(Page::Count);
static_assert(sizeof(PAGE_NAMES) / sizeof(PAGE_NAMES[0]) == PAGES, "one name per page");
static_assert(sizeof(TITLES) / sizeof(TITLES[0]) == PAGES, "one title per page");

/** @brief A bottom tab and the consecutive pages under it. */
struct Group {
  const char *name;
  Page first;
  uint8_t count;
};
const Group GROUPS[] = {
    {"Weather", Page::Now, 4},  {"Air", Page::Air, 3},     {"Flights", Page::Flights, 1}, {"History", Page::History, 1},
    {"Home", Page::Home, 2},    {"Network", Page::Wifi, 5}, {"System", Page::System, 2},
};
constexpr int GROUPS_N = sizeof(GROUPS) / sizeof(GROUPS[0]);
Page lastInGroup[GROUPS_N];  ///< Page to reopen when a bottom tab is tapped (initialised in begin()).

int groupIndex(Page p) {
  for (int g = 0; g < GROUPS_N; ++g) {
    const int f = static_cast<int>(GROUPS[g].first);
    if (static_cast<int>(p) >= f && static_cast<int>(p) < f + GROUPS[g].count) return g;
  }
  return 0;
}

/** @brief Sub-tab strip geometry for the current group. */
constexpr int SEG_W = 80, SEG_H = 32, SEG_Y = (TOP_H - SEG_H) / 2;
int segX0(const Group &g) { return W / 2 - g.count * SEG_W / 2; }

bool alertActive() {
  return DataService::warnings().count > 0 ||
         (DataService::aurora().valid && DataService::aurora().level >= AuroraLevel::Amber) ||
         Frost::assess(DataService::weather(), DataService::tempest()).level >= FrostLevel::Moderate;
}

/** @brief Clock, date, sub-tabs (or the page title) and status icons. */
void statusBar() {
  canvas.fillRect(0, 0, W, TOP_H, C::BAR);
  tm t;
  if (Net::localTime(t)) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    const int w = text(buf, 12, TOP_H / 2, F::M, C::TEXT, td::middle_left);
    strftime(buf, sizeof(buf), "%a %d %b", &t);
    if (GROUPS[groupIndex(state.page)].count < 5) text(buf, 12 + w + 12, TOP_H / 2 + 1, F::S, C::MUTED, td::middle_left);
  }
  const Group &g = GROUPS[groupIndex(state.page)];
  int centreRight = W / 2;  // Right edge of the centre element, so status chips don't overlap it.
  if (g.count == 1) {
    text(TITLES[static_cast<int>(state.page)], W / 2, TOP_H / 2, F::M, C::TEXT, td::middle_center);
    centreRight = W / 2 + textWidth(TITLES[static_cast<int>(state.page)], F::M) / 2;
  } else {
    const int x0 = segX0(g);
    canvas.fillRoundRect(x0 - 3, SEG_Y - 3, g.count * SEG_W + 6, SEG_H + 6, 10, C::BG);
    for (int i = 0; i < g.count; ++i) {
      const Page p = static_cast<Page>(static_cast<int>(g.first) + i);
      const bool on = p == state.page;
      const int x = x0 + i * SEG_W;
      if (on) canvas.fillRoundRect(x, SEG_Y, SEG_W, SEG_H, 8, C::CARD2);
      const char *n = PAGE_NAMES[static_cast<int>(p)];
      const F f = textWidth(n, F::S) <= SEG_W - 10 ? F::S : F::XS;
      text(n, x + SEG_W / 2, TOP_H / 2 + 1, f, on ? C::ACCENT : C::MUTED, td::middle_center);
      if (p == Page::Alerts && alertActive()) canvas.fillCircle(x + SEG_W - 8, SEG_Y + 7, 4, C::ORANGE);
    }
    centreRight = x0 + g.count * SEG_W + 3;
  }

  // Right-hand icons, laid out right to left; optional chips only if they fit.
  int x = W - 12 - 24;
  wifiBars(x, 13, Net::bars(), Net::connected());
  x -= 12;

  const WarningList &wl = DataService::warnings();
  if (wl.count) {
    x -= 24;
    warningTriangle(x, 10, 24, warningColor(wl.highest()));
    x -= 10;
  }
  const TempestData &tp = DataService::tempest();
  const char *tl = tp.fresh() ? "Tempest" : "Tempest?";
  x -= textWidth(tl, F::XS) + 20;
  chip(x, 8, tl, tp.fresh() ? rgb(28, 80, 48) : C::CARD2, tp.fresh() ? C::GREEN : C::DIM, F::XS);
  x -= 10;
  const FrostRisk fr = Frost::assess(DataService::weather(), DataService::tempest());
  if (fr.level >= FrostLevel::Moderate && x - textWidth("FROST", F::XS) - 20 > centreRight + 8) {
    x -= textWidth("FROST", F::XS) + 20;
    chip(x, 8, "FROST", frostColor(fr.level), C::BG, F::XS);
    x -= 10;
  }
  const AuroraData &au = DataService::aurora();
  if (au.valid && au.level >= AuroraLevel::Amber && x - textWidth("AURORA", F::XS) - 20 > centreRight + 8) {
    x -= textWidth("AURORA", F::XS) + 20;
    chip(x, 8, "AURORA", auroraColor(au.level), C::BG, F::XS);
  }
}

/** @brief Bottom tabs (one per group), alert dot on Weather. */
void tabBar() {
  canvas.fillRect(0, TAB_Y, W, TAB_H, C::BAR);
  canvas.drawFastHLine(0, TAB_Y, W, C::LINE);
  const int cur = groupIndex(state.page);
  const bool alert = alertActive();
  for (int i = 0; i < GROUPS_N; ++i) {
    const int x0 = i * W / GROUPS_N, x1 = (i + 1) * W / GROUPS_N;
    const bool active = i == cur;
    if (active) {
      canvas.fillRect(x0, TAB_Y + 1, x1 - x0, TAB_H - 1, C::CARD2);
      canvas.fillRect(x0, TAB_Y, x1 - x0, 3, C::ACCENT);
    }
    text(GROUPS[i].name, (x0 + x1) / 2, TAB_Y + TAB_H / 2 + 1, F::S, active ? C::ACCENT : C::MUTED, td::middle_center);
    if (GROUPS[i].count > 1) {  // Dots: how many sub-pages, which one is open.
      const int n = GROUPS[i].count, dx = 9, sx = (x0 + x1) / 2 - (n - 1) * dx / 2;
      for (int k = 0; k < n; ++k) {
        const bool on = active && static_cast<int>(state.page) == static_cast<int>(GROUPS[i].first) + k;
        canvas.fillCircle(sx + k * dx, TAB_Y + TAB_H - 9, 2, on ? C::ACCENT : C::LINE);
      }
    }
    if (GROUPS[i].first == Page::Now && alert) canvas.fillCircle(x1 - 14, TAB_Y + 12, 5, C::ORANGE);
    if (i > 0) canvas.drawFastVLine(x0, TAB_Y + 10, TAB_H - 20, C::LINE);
  }
}

/** @brief Draw @p page completely. Caller holds the canvas and data locks. */
void compose(Page page) {
  const Page saved = state.page;
  state.page = page;
  clearButtons(page);
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
    case Page::Home: pageHome(); break;
    case Page::Lab: pageLab(); break;
    case Page::Wifi: pageWifi(); break;
    case Page::NetCrowd: pageNetCrowd(); break;
    case Page::NetTest: pageNetTest(); break;
    case Page::NetMonitor: pageNetMonitor(); break;
    case Page::NetHelp: pageNetHelp(); break;
    case Page::System: pageSystem(); break;
    case Page::Settings: pageSettings(); break;
    case Page::Count: break;
  }
  statusBar();
  tabBar();
  state.page = saved;
}

/** @brief Route a tap to the tab bar, status bar or the visible page. */
void tap(int x, int y) {
  if (y >= TAB_Y) {
    const int g = constrain(x * GROUPS_N / W, 0, GROUPS_N - 1);
    // Tapping the open group's tab again cycles its sub-pages; otherwise reopen the last one used.
    if (g == groupIndex(state.page) && GROUPS[g].count > 1) {
      const int f = static_cast<int>(GROUPS[g].first);
      Ui::show(static_cast<Page>(f + (static_cast<int>(state.page) - f + 1) % GROUPS[g].count));
    } else {
      Ui::show(lastInGroup[g]);
    }
    return;
  }
  if (y < TOP_H) {
    const Group &g = GROUPS[groupIndex(state.page)];
    const int x0 = segX0(g);
    if (g.count > 1 && x >= x0 && x < x0 + g.count * SEG_W) {
      Ui::show(static_cast<Page>(static_cast<int>(g.first) + (x - x0) / SEG_W));
    } else if (x > W - 260) {
      Ui::show(Page::Alerts);  // Status icons lead to Alerts.
    }
    return;
  }
  switch (state.page) {
    case Page::Radar: tapRadar(x, y); break;
    case Page::Flights: tapFlights(x, y); break;
    case Page::History: tapHistory(x, y); break;
    case Page::Wifi: tapWifi(x, y); break;
    case Page::NetCrowd: tapNetCrowd(x, y); break;
    case Page::NetTest: tapNetTest(x, y); break;
    case Page::NetMonitor: tapNetMonitor(x, y); break;
    case Page::Settings: tapSettings(x, y); break;
    default: break;
  }
}

/** @brief Route a vertical drag to the visible page. */
void drag(int dy) {
  switch (state.page) {
    case Page::Flights: dragFlights(dy); break;
    case Page::Wifi: dragWifi(dy); break;
    case Page::NetMonitor: dragNetMonitor(dy); break;
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
  for (int g = 0; g < GROUPS_N; ++g) lastInGroup[g] = GROUPS[g].first;
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  if (!canvas.createSprite(W, H)) {
    Log::println("[ui] sprite allocation failed");
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
  if (down) stats_.lastTouchMs = millis();
  if (down && !wasDown) {
    startX = lastX = x;
    startY = lastY = y;
  } else if (down) {
    lastX = x;
    lastY = y;
  } else if (wasDown) {
    const int dx = lastX - startX, dy = lastY - startY;
    if (abs(dx) > CP_SWIPE_MIN_PX && abs(dx) > 2 * abs(dy) && startY < TAB_Y) {
      const int n = PAGES;
      const int p = (static_cast<int>(state.page) + (dx < 0 ? 1 : n - 1)) % n;
      show(static_cast<Page>(p));
      Log::printf("[touch] swipe %s\n", dx < 0 ? "left" : "right");
    } else if (abs(dy) > CP_DRAG_MIN_PX && abs(dy) > 2 * abs(dx) && startY >= TOP_H && startY < TAB_Y) {
      CanvasLock c;  // Pages read hit-test state the renderer writes.
      drag(dy);
      stats_.lastTapAtMs = millis();  // Ask the loop for an immediate redraw.
    } else if (abs(dx) < CP_TAP_MAX_MOVE_PX && abs(dy) < CP_TAP_MAX_MOVE_PX) {
      Log::printf("[touch] tap %d,%d\n", startX, startY);
      stats_.lastTapX = startX;
      stats_.lastTapY = startY;
      stats_.lastTapAtMs = millis();
      CanvasLock c;  // Pages read hit-test state the renderer writes.
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
  lastInGroup[groupIndex(p)] = p;
  Log::printf("[ui] page %s\n", pageName(p));
}

bool rotatable(Page p) {
  return p != Page::Settings && p != Page::NetCrowd && p != Page::NetTest && p != Page::NetMonitor && p != Page::NetHelp &&
         p < Page::Count;
}

void advance() {
  const uint32_t mask = Settings::get().autoPageMask;
  for (int k = 1; k <= PAGES; ++k) {
    const Page p = static_cast<Page>((static_cast<int>(state.page) + k) % PAGES);
    if (rotatable(p) && (mask & (1UL << static_cast<int>(p)))) {
      show(p);
      return;
    }
  }
}

const char *pageName(Page p) {
  const int i = static_cast<int>(p);
  return i < PAGES ? PAGE_NAMES[i] : "?";
}

}  // namespace Ui
