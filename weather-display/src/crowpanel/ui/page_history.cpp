/**
 * @file    page_history.cpp
 * @brief   "History" page: on-device charts from the quarter-hourly log.
 *
 * @details Four charts (temperature, pressure, wind, rain) over 24 h, 3 days
 *          or 7 days, selected with the buttons at the top. Tempest values are
 *          used where logged, Open-Meteo otherwise. Records are cached and
 *          re-read from flash when the range changes or every minute.
 */
#include <vector>

#include "history.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

std::vector<HistoryRecord> cache;  ///< Records for the current range (oldest first).
uint8_t cacheDays = 0;             ///< Range the cache was loaded for.
uint32_t cacheAtMs = 0;            ///< When it was loaded.

constexpr int BTN_Y = BODY_Y + 6;  ///< Range buttons row.
constexpr int BTN_W = 96, BTN_H = 34;
const uint8_t RANGES[] = {1, 3, 7};

/** @brief x of range button @p i (right-aligned). */
int buttonX(int i) { return W - M - (3 - i) * (BTN_W + 8); }

void reload() {
  const time_t now = time(nullptr);
  History::read(static_cast<uint32_t>(now - state.historyDays * 86400L), cache);
  cacheDays = state.historyDays;
  cacheAtMs = millis();
}

/** @brief Extracts one series value; returns NAN when missing. */
using Getter = float (*)(const HistoryRecord &);

/**
 * @brief One chart card.
 * @param bars  Draw as bars (rain) instead of lines.
 * @param zeroBase  Y axis starts at 0.
 */
void chart(int x, int y, int w, int h, const char *title, const char *unit, Getter primary, Getter secondary,
           uint16_t c1, uint16_t c2, bool bars, bool zeroBase) {
  card(x, y, w, h, title);
  const int n = cache.size();
  float lo = 1e9, hi = -1e9, last = NAN, sum = 0;
  for (const HistoryRecord &r : cache) {
    for (Getter g : {primary, secondary}) {
      if (!g) continue;
      const float v = g(r);
      if (isnan(v)) continue;
      lo = min(lo, v);
      hi = max(hi, v);
    }
    const float v = primary(r);
    if (!isnan(v)) {
      last = v;
      sum += v;
    }
  }
  if (n < 2 || lo > hi) return placeholder(x, y, w, h, "Not enough data yet");
  if (zeroBase) lo = 0;
  if (hi - lo < 1) hi = lo + 1;

  if (bars) textf(x + w - 14, y + 10, F::S, c1, td::top_right, "%.1f %s total", sum, unit);
  else textf(x + w - 14, y + 10, F::S, c1, td::top_right, "%.1f %s", last, unit);

  const int gx0 = x + 46, gx1 = x + w - 12, gy0 = y + 40, gy1 = y + h - 26;
  textf(gx0 - 6, gy0, F::XS, C::DIM, td::top_right, "%.0f", hi);
  textf(gx0 - 6, gy1, F::XS, C::DIM, td::bottom_right, "%.0f", lo);
  canvas.drawFastHLine(gx0, gy1, gx1 - gx0, C::LINE);

  const uint32_t t0 = cache.front().time, t1 = cache.back().time;
  auto px = [&](uint32_t t) { return gx0 + static_cast<int>((uint64_t)(t - t0) * (gx1 - gx0) / max<uint32_t>(1, t1 - t0)); };
  auto py = [&](float v) { return gy1 - static_cast<int>((v - lo) / (hi - lo) * (gy1 - gy0)); };

  for (int s = 1; s >= 0; --s) {  // Secondary first, primary on top.
    Getter g = s ? secondary : primary;
    if (!g) continue;
    const uint16_t col = s ? c2 : c1;
    int lx = -1, ly = 0;
    for (const HistoryRecord &r : cache) {
      const float v = g(r);
      if (isnan(v)) {
        lx = -1;
        continue;
      }
      const int cx = px(r.time), cy = py(v);
      if (bars) {
        if (cy < gy1) canvas.drawFastVLine(cx, cy, gy1 - cy, col);
      } else if (lx >= 0 && cx - lx < 40) {
        canvas.drawLine(lx, ly, cx, cy, col);
      }
      lx = cx;
      ly = cy;
    }
  }
  // Time labels: start and end.
  char a[16], b[16];
  const time_t ta = t0, tb = t1;
  tm la, lb;
  localtime_r(&ta, &la);
  localtime_r(&tb, &lb);
  strftime(a, sizeof(a), state.historyDays > 1 ? "%a %H:%M" : "%H:%M", &la);
  strftime(b, sizeof(b), state.historyDays > 1 ? "%a %H:%M" : "%H:%M", &lb);
  text(a, gx0, gy1 + 4, F::XS, C::DIM);
  text(b, gx1, gy1 + 4, F::XS, C::DIM, td::top_right);
}

float tTemp(const HistoryRecord &r) {
  if (r.flags & HF_TEMPEST) return r.tTemp10 / 10.0f;
  return r.flags & HF_WEATHER ? r.temp10 / 10.0f : NAN;
}
float tPress(const HistoryRecord &r) {
  if (r.flags & HF_TEMPEST) return r.tPressure10 / 10.0f;
  return r.flags & HF_WEATHER ? r.pressure10 / 10.0f : NAN;
}
float tWind(const HistoryRecord &r) {
  if (r.flags & HF_TEMPEST) return r.tWind10 / 10.0f;
  return r.flags & HF_WEATHER ? r.wind10 / 10.0f : NAN;
}
float tGust(const HistoryRecord &r) {
  if (r.flags & HF_TEMPEST) return r.tGust10 / 10.0f;
  return r.flags & HF_WEATHER ? r.gust10 / 10.0f : NAN;
}
float tRain(const HistoryRecord &r) {
  if (r.flags & HF_TEMPEST) return r.tRain100 / 100.0f;
  return r.flags & HF_WEATHER ? r.rain100 / 100.0f : NAN;
}

}  // namespace

void pageHistory() {
  if (cacheDays != state.historyDays || millis() - cacheAtMs > 60000) reload();

  textf(M + 6, BODY_Y + 14, F::S, C::MUTED, td::top_left, "%u records, every 15 min", static_cast<unsigned>(cache.size()));
  for (int i = 0; i < 3; ++i) {
    const bool on = state.historyDays == RANGES[i];
    canvas.fillRoundRect(buttonX(i), BTN_Y, BTN_W, BTN_H, 8, on ? C::ACCENT : C::CARD2);
    textf(buttonX(i) + BTN_W / 2, BTN_Y + BTN_H / 2, F::S, on ? C::BG : C::TEXT, td::middle_center,
          RANGES[i] == 1 ? "24 h" : "%u days", RANGES[i]);
  }

  constexpr int top = BODY_Y + 46, cw = (W - 3 * M) / 2, ch = (BODY_H - 46 - 2 * M) / 2;
  chart(M, top, cw, ch, "Temperature", "C", tTemp, nullptr, C::ORANGE, 0, false, false);
  chart(2 * M + cw, top, cw, ch, "Pressure", "hPa", tPress, nullptr, C::GREEN, 0, false, false);
  chart(M, top + ch + M, cw, ch, "Wind / gust", "mph", tWind, tGust, C::CYAN, C::PURPLE, false, true);
  chart(2 * M + cw, top + ch + M, cw, ch, "Rain", "mm", tRain, nullptr, C::BLUE, 0, true, true);
}

bool tapHistory(int x, int y) {
  if (y < BTN_Y || y > BTN_Y + BTN_H) return false;
  for (int i = 0; i < 3; ++i) {
    if (x >= buttonX(i) && x < buttonX(i) + BTN_W) {
      state.historyDays = RANGES[i];
      return true;
    }
  }
  return false;
}

}  // namespace UiDraw
