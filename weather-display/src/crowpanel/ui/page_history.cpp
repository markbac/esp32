/**
 * @file    page_history.cpp
 * @brief   "History" page: records & summaries, and charts from the 90-day log.
 *
 * @details Modes (buttons top-right): Summary, 24 h, 7 d, 30 d, 90 d.
 *          - Summary: today and yesterday (high/low with times, strongest gust,
 *            rain and peak rate, lightning) plus a records table for the last
 *            7, 30 and 90 days (extremes with dates, total rain, wettest day,
 *            days with rain).
 *          - Charts: temperature, pressure, wind/gust, rain. Long ranges are
 *            reduced per pixel column (min/max for lines, sums for rain).
 *          Tempest values are used where logged, Open-Meteo otherwise.
 */
#include <vector>

#include "history.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

std::vector<HistoryRecord> cache;  ///< All records in the loaded span (oldest first).
uint32_t cacheAtMs = 0;            ///< When it was loaded (0 = never).
uint8_t cacheDays = 0;             ///< Span loaded.

constexpr int BTN_Y = BODY_Y + 6;
constexpr int BTN_W = 86, BTN_H = 34;
const uint8_t MODES[] = {0, 1, 7, 30, 90};  ///< 0 = summary.

int buttonX(int i) { return W - M - (5 - i) * (BTN_W + 6); }

/** @brief Load @p days of history (cached for 2 minutes). */
void ensure(uint8_t days) {
  // Reload when the range grows, every 2 minutes, or when the log has gained records.
  static uint16_t seenCount = 0;
  if (cacheDays >= days && cacheAtMs && millis() - cacheAtMs < 120000 && seenCount == History::count()) return;
  seenCount = History::count();
  const time_t now = time(nullptr);
  History::read(static_cast<uint32_t>(now - days * 86400L), cache);
  cacheDays = days;
  cacheAtMs = millis();
}

// ---- field getters (Tempest preferred) --------------------------------------
float vTemp(const HistoryRecord &r) {
  if (r.flags & HF_TEMPEST) return r.tTemp10 / 10.0f;
  return r.flags & HF_WEATHER ? r.temp10 / 10.0f : NAN;
}
float vPress(const HistoryRecord &r) {
  if (r.flags & HF_TEMPEST) return r.tPressure10 / 10.0f;
  return r.flags & HF_WEATHER ? r.pressure10 / 10.0f : NAN;
}
float vWind(const HistoryRecord &r) {
  if (r.flags & HF_TEMPEST) return r.tWind10 / 10.0f;
  return r.flags & HF_WEATHER ? r.wind10 / 10.0f : NAN;
}
float vGust(const HistoryRecord &r) {
  if (r.flags & HF_TEMPEST) return r.tGust10 / 10.0f;
  return r.flags & HF_WEATHER ? r.gust10 / 10.0f : NAN;
}
float vRain(const HistoryRecord &r) {
  if (r.flags & HF_TEMPEST) return r.tRain100 / 100.0f;
  return r.flags & HF_WEATHER ? r.rain100 / 100.0f : NAN;
}
float vRate(const HistoryRecord &r) { return r.flags & HF_TEMPEST ? r.tRate10 / 10.0f : NAN; }

using Getter = float (*)(const HistoryRecord &);

// ---- charts -------------------------------------------------------------------
void chart(int x, int y, int w, int h, const char *title, const char *unit, Getter primary, Getter secondary,
           uint16_t c1, uint16_t c2, bool bars, bool zeroBase, uint32_t since) {
  card(x, y, w, h, title);
  // Records in range.
  size_t first = 0;
  while (first < cache.size() && cache[first].time < since) ++first;
  const size_t n = cache.size() - first;
  if (n < 2) return placeholder(x, y, w, h, "Not enough data yet");

  const int gx0 = x + 46, gx1 = x + w - 12, gy0 = y + 40, gy1 = y + h - 26;
  const int cols = gx1 - gx0;
  const uint32_t t0 = cache[first].time, t1 = cache.back().time;
  auto colOf = [&](uint32_t t) { return static_cast<int>((uint64_t)(t - t0) * (cols - 1) / max<uint32_t>(1, t1 - t0)); };

  // Reduce to per-column min/max (lines) or sums (bars) for each series.
  struct Col { float lo, hi, sum; bool any; };
  static Col series[2][400];
  float lo = 1e9, hi = -1e9, last = NAN, total = 0;
  for (int s = 0; s < 2; ++s) {
    Getter g = s ? secondary : primary;
    for (int c = 0; c < cols && c < 400; ++c) series[s][c] = {1e9f, -1e9f, 0, false};
    if (!g) continue;
    for (size_t i = first; i < cache.size(); ++i) {
      const float v = g(cache[i]);
      if (isnan(v)) continue;
      Col &c = series[s][min(colOf(cache[i].time), 399)];
      c.lo = min(c.lo, v);
      c.hi = max(c.hi, v);
      c.sum += v;
      c.any = true;
      if (s == 0) {
        last = v;
        total += v;
      }
    }
    for (int c = 0; c < cols && c < 400; ++c) {
      if (!series[s][c].any) continue;
      lo = min(lo, bars ? 0 : series[s][c].lo);
      hi = max(hi, bars ? series[s][c].sum : series[s][c].hi);
    }
  }
  if (lo > hi) return placeholder(x, y, w, h, "No data in this range");
  if (zeroBase) lo = 0;
  if (hi - lo < 1) hi = lo + 1;
  auto py = [&](float v) { return gy1 - static_cast<int>((v - lo) / (hi - lo) * (gy1 - gy0)); };

  if (bars) textf(x + w - 14, y + 10, F::S, c1, td::top_right, "%.1f %s total", total, unit);
  else textf(x + w - 14, y + 10, F::S, c1, td::top_right, "%.1f %s", last, unit);
  textf(gx0 - 6, gy0, F::XS, C::DIM, td::top_right, "%.0f", hi);
  textf(gx0 - 6, gy1, F::XS, C::DIM, td::bottom_right, "%.0f", lo);
  canvas.drawFastHLine(gx0, gy1, cols, C::LINE);

  for (int s = 1; s >= 0; --s) {
    if (!(s ? secondary : primary)) continue;
    const uint16_t col = s ? c2 : c1;
    int lx = -1, ly = 0;
    for (int c = 0; c < cols && c < 400; ++c) {
      const Col &v = series[s][c];
      if (!v.any) continue;
      if (bars) {
        const int top = py(v.sum);
        if (top < gy1) canvas.drawFastVLine(gx0 + c, top, gy1 - top, col);
        continue;
      }
      const int yHi = py(v.hi), yLo = py(v.lo);
      canvas.drawFastVLine(gx0 + c, yHi, max(1, yLo - yHi + 1), col);  // Range within the column.
      const int mid = (yHi + yLo) / 2;
      if (lx >= 0 && c - lx < 12) canvas.drawLine(gx0 + lx, ly, gx0 + c, mid, col);
      lx = c;
      ly = mid;
    }
  }

  char a[16], b[16];
  const time_t ta = t0, tb = t1;
  tm la, lb;
  localtime_r(&ta, &la);
  localtime_r(&tb, &lb);
  const char *fmt = state.historyDays > 1 ? "%d %b" : "%H:%M";
  strftime(a, sizeof(a), fmt, &la);
  strftime(b, sizeof(b), fmt, &lb);
  text(a, gx0, gy1 + 4, F::XS, C::DIM);
  text(b, gx1, gy1 + 4, F::XS, C::DIM, td::top_right);
}

void charts() {
  const uint8_t days = state.historyDays;
  ensure(days);
  const uint32_t since = static_cast<uint32_t>(time(nullptr) - days * 86400L);
  constexpr int top = BODY_Y + 46, cw = (W - 3 * M) / 2, ch = (BODY_H - 46 - 2 * M) / 2;
  chart(M, top, cw, ch, "Temperature", "C", vTemp, nullptr, C::ORANGE, 0, false, false, since);
  chart(2 * M + cw, top, cw, ch, "Pressure", "hPa", vPress, nullptr, C::GREEN, 0, false, false, since);
  chart(M, top + ch + M, cw, ch, "Wind / gust", "mph", vWind, vGust, C::CYAN, C::PURPLE, false, true, since);
  chart(2 * M + cw, top + ch + M, cw, ch, days > 7 ? "Rain (per column)" : "Rain", "mm", vRain, nullptr, C::BLUE, 0, true,
        true, since);
}

// ---- summaries ----------------------------------------------------------------
/** @brief Extremes and totals over a time span. */
struct Stats {
  float hi = NAN, lo = NAN, gust = NAN, rain = 0, rate = NAN;
  uint32_t hiT = 0, loT = 0, gustT = 0;
  uint16_t strikes = 0;
  int records = 0;
};

Stats statsBetween(uint32_t from, uint32_t to) {
  Stats s;
  for (const HistoryRecord &r : cache) {
    if (r.time < from || r.time >= to) continue;
    ++s.records;
    const float t = vTemp(r), g = vGust(r), rn = vRain(r), rt = vRate(r);
    if (!isnan(t) && (isnan(s.hi) || t > s.hi)) { s.hi = t; s.hiT = r.time; }
    if (!isnan(t) && (isnan(s.lo) || t < s.lo)) { s.lo = t; s.loT = r.time; }
    if (!isnan(g) && (isnan(s.gust) || g > s.gust)) { s.gust = g; s.gustT = r.time; }
    if (!isnan(rn)) s.rain += rn;
    if (!isnan(rt) && (isnan(s.rate) || rt > s.rate)) s.rate = rt;
    if (r.flags & HF_TEMPEST) s.strikes += r.tStrikes;
  }
  return s;
}

/** @brief Today / yesterday card. */
void dayCard(int x, int y, int w, int h, const char *title, const Stats &s) {
  card(x, y, w, h, title);
  if (!s.records) return placeholder(x, y, w, h, "No records");
  char t1[6], t2[6], t3[6], buf[48];
  hhmm(s.hiT, t1);
  hhmm(s.loT, t2);
  hhmm(s.gustT, t3);
  auto row = [&](int ry, const char *label, const char *val, uint16_t col) {
    text(label, x + 16, ry, F::XS, C::MUTED);
    text(val, x + w - 16, ry - 3, F::S, col, td::top_right);
  };
  snprintf(buf, sizeof(buf), "%.1f C at %s", s.hi, t1);
  row(y + 40, "High", buf, C::ORANGE);
  snprintf(buf, sizeof(buf), "%.1f C at %s", s.lo, t2);
  row(y + 66, "Low", buf, C::CYAN);
  snprintf(buf, sizeof(buf), "%.0f mph at %s", s.gust, t3);
  row(y + 92, "Strongest gust", buf, C::PURPLE);
  if (isnan(s.rate)) snprintf(buf, sizeof(buf), "%.1f mm", s.rain);
  else snprintf(buf, sizeof(buf), "%.1f mm, peak %.1f mm/h", s.rain, s.rate);
  row(y + 118, "Rain", buf, C::BLUE);
  if (s.strikes) {
    snprintf(buf, sizeof(buf), "%u strikes", s.strikes);
    row(y + 144, "Lightning", buf, C::PURPLE);
  }
}

/** @brief Records table for 7 / 30 / 90 days. */
void recordsCard(uint32_t midnight) {
  constexpr int x = M, y = BODY_Y + 230, w = W - 2 * M, h = BODY_H - 230 - M;
  card(x, y, w, h, "Records");
  const int spans[] = {7, 30, 90};
  const int colX[] = {x + 250, x + 430, x + 610};
  for (int i = 0; i < 3; ++i) textf(colX[i], y + 12, F::XS, C::MUTED, td::top_left, "Last %d days", spans[i]);

  const char *labels[] = {"Highest", "Lowest", "Strongest gust", "Total rain", "Wettest day"};
  for (int r = 0; r < 5; ++r) text(labels[r], x + 16, y + 36 + r * 22, F::XS, C::MUTED);

  for (int i = 0; i < 3; ++i) {
    const uint32_t from = midnight + 86400 - spans[i] * 86400UL;
    const Stats s = statsBetween(from, UINT32_MAX);
    // Rain per local day for the wettest day and rain days.
    float day[91] = {};
    for (const HistoryRecord &rec : cache) {
      if (rec.time < from) continue;
      const float v = vRain(rec);
      if (isnan(v)) continue;
      const int64_t diff = static_cast<int64_t>(rec.time) - midnight;  // Floor division: yesterday = -1.
      const int d = static_cast<int>(diff >= 0 ? diff / 86400 : -((-diff + 86399) / 86400)) + 90;
      if (d >= 0 && d <= 90) day[d] += v;
    }
    int wettest = -1, rainDays = 0;
    for (int d = 0; d <= 90; ++d) {
      if (day[d] >= 0.2f) ++rainDays;
      if (wettest < 0 || day[d] > day[wettest]) wettest = d;
    }
    char date[12], buf[40];
    auto dstr = [&](uint32_t t) {
      const time_t tt = t;
      tm lt;
      localtime_r(&tt, &lt);
      strftime(date, sizeof(date), "%d %b", &lt);
      return date;
    };
    const int cx = colX[i];
    if (!s.records) {
      text("--", cx, y + 36, F::XS, C::DIM);
      continue;
    }
    snprintf(buf, sizeof(buf), "%.1f C  %s", s.hi, dstr(s.hiT));
    text(buf, cx, y + 36, F::XS, C::ORANGE);
    snprintf(buf, sizeof(buf), "%.1f C  %s", s.lo, dstr(s.loT));
    text(buf, cx, y + 58, F::XS, C::CYAN);
    snprintf(buf, sizeof(buf), "%.0f mph  %s", s.gust, dstr(s.gustT));
    text(buf, cx, y + 80, F::XS, C::PURPLE);
    snprintf(buf, sizeof(buf), "%.1f mm  (%d wet days)", s.rain, rainDays);
    text(buf, cx, y + 102, F::XS, C::BLUE);
    if (wettest >= 0 && day[wettest] > 0) {
      snprintf(buf, sizeof(buf), "%.1f mm  %s", day[wettest], dstr(midnight + (wettest - 90) * 86400L));
      text(buf, cx, y + 124, F::XS, C::BLUE);
    } else {
      text("dry", cx, y + 124, F::XS, C::DIM);
    }
  }
}

void summary() {
  ensure(HISTORY_DAYS > 90 ? 90 : HISTORY_DAYS);
  tm lt;
  const time_t now = time(nullptr);
  localtime_r(&now, &lt);
  lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
  const uint32_t midnight = static_cast<uint32_t>(mktime(&lt));
  constexpr int cw = (W - 3 * M) / 2;
  dayCard(M, BODY_Y + 46, cw, 176, "Today", statsBetween(midnight, UINT32_MAX));
  dayCard(2 * M + cw, BODY_Y + 46, cw, 176, "Yesterday", statsBetween(midnight - 86400, midnight));
  recordsCard(midnight);
}

}  // namespace

void pageHistory() {
  textf(M + 6, BODY_Y + 14, F::S, C::MUTED, td::top_left, "%u records (%u days kept)", History::count(),
        static_cast<unsigned>(HISTORY_DAYS));
  for (int i = 0; i < 5; ++i) {
    const bool on = state.historyDays == MODES[i];
    canvas.fillRoundRect(buttonX(i), BTN_Y, BTN_W, BTN_H, 8, on ? C::ACCENT : C::CARD2);
    char label[12];
    if (MODES[i] == 0) snprintf(label, sizeof(label), "Summary");
    else if (MODES[i] == 1) snprintf(label, sizeof(label), "24 h");
    else snprintf(label, sizeof(label), "%u d", MODES[i]);
    const F bf = textWidth(label, F::S) <= BTN_W - 10 ? F::S : F::XS;
    text(label, buttonX(i) + BTN_W / 2, BTN_Y + BTN_H / 2, bf, on ? C::BG : C::TEXT, td::middle_center);
  }
  if (state.historyDays == 0) summary();
  else charts();
}

bool tapHistory(int x, int y) {
  if (y < BTN_Y || y > BTN_Y + BTN_H) return false;
  for (int i = 0; i < 5; ++i) {
    if (x >= buttonX(i) && x < buttonX(i) + BTN_W) {
      state.historyDays = MODES[i];
      return true;
    }
  }
  return false;
}

}  // namespace UiDraw
