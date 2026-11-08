/**
 * @file    page_alerts.cpp
 * @brief   "Alerts" page: Met Office warnings, AuroraWatch UK status and Tempest lightning.
 */
#include "config.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

void warningsCard(const WarningList &wl) {
  constexpr int x = M, y = BODY_Y + M, w = 520, h = BODY_H - 2 * M;
  card(x, y, w, h, "Met Office warnings - " WARNING_AREA_FILTER);
  if (!wl.valid) return placeholder(x, y, w, h, "Checking Met Office...");
  if (wl.count == 0) {
    const int cx = x + w / 2, cy = y + 150;
    canvas.fillCircle(cx, cy, 46, C::GREEN);
    canvas.drawWideLine(cx - 22, cy, cx - 6, cy + 18, 5, C::CARD);
    canvas.drawWideLine(cx - 6, cy + 18, cx + 24, cy - 16, 5, C::CARD);
    text("No warnings in force", cx, cy + 70, F::M, C::TEXT, td::top_center);
    return;
  }
  const int shown = min<int>(wl.count, 3);
  for (int i = 0; i < shown; ++i) {
    const WeatherWarning &wn = wl.items[i];
    const int cy = y + 40 + i * 104;
    const uint16_t col = warningColor(wn.level);
    canvas.fillRoundRect(x + 12, cy, w - 24, 94, 8, C::CARD2);
    canvas.fillRoundRect(x + 12, cy, 8, 94, 4, col);
    warningTriangle(x + 34, cy + 14, 40, col);
    textf(x + 90, cy + 12, F::M, col, td::top_left, "%s warning", Warnings::levelName(wn.level));
    text(wn.hazard, x + 90, cy + 42, F::M, C::TEXT);
    text(wn.validity, x + 34, cy + 70, F::XS, C::MUTED);
  }
  if (wl.total > shown) textf(x + w - 16, y + 12, F::XS, C::MUTED, td::top_right, "+%d more", wl.total - shown);
}

void auroraCard(const AuroraData &a) {
  constexpr int x = 536, y = BODY_Y + M, w = W - 536 - M, h = 228;
  card(x, y, w, h, "Aurora");
  text("AuroraWatch UK", x + w - 14, y + 13, F::XS, C::DIM, td::top_right);
  if (!a.valid) return placeholder(x, y, w, h, "Checking...");
  const uint16_t col = auroraColor(a.level);
  text(Aurora::levelName(a.level), x + 16, y + 36, F::L, col);
  textf(x + w - 16, y + 50, F::S, C::TEXT, td::top_right, "%.0f nT", Aurora::latestActivity(a));
  text(Aurora::meaning(a.level), x + 16, y + 86, F::XS, a.level >= AuroraLevel::Amber ? col : C::MUTED);

  // Last 24 hours, 200 nT (red) fills the chart; dotted lines at 50 and 100 nT.
  const int cx0 = x + 16, cw = w - 32, cBot = y + h - 16, cH = 100;
  const int bw = max(2, cw / AURORA_HOURS);
  for (int i = 0; i < a.count; ++i) {
    const int bh = constrain(static_cast<int>(a.activity[i] / 200.0f * cH), 1, cH);
    canvas.fillRect(cx0 + i * bw, cBot - bh, bw - 2, bh, auroraColor(a.hourLevel[i]));
  }
  for (int lvl : {50, 100}) {
    const int ly = cBot - lvl * cH / 200;
    for (int px = cx0; px < cx0 + cw; px += 4) canvas.drawPixel(px, ly, lvl == 100 ? C::ORANGE : C::YELLOW);
  }
  canvas.drawFastHLine(cx0, cBot, cw, C::LINE);
}

void lightningCard(const TempestData &t) {
  constexpr int x = 536, y = BODY_Y + 244, w = W - 536 - M, h = BODY_H - 244 - M;
  card(x, y, w, h, "Lightning - Tempest");
  if (!t.valid) return placeholder(x, y, w, h, "No Tempest data");
  textf(x + 16, y + 36, F::M, t.strikesToday ? C::PURPLE : C::TEXT, td::top_left, "%u today", t.strikesToday);
  textf(x + w - 16, y + 42, F::S, C::MUTED, td::top_right, "%u last hour", t.strikesHour);
  if (t.lastStrikeTime) {
    char hm[6];
    hhmm(t.lastStrikeTime, hm);
    textf(x + 16, y + 80, F::XS, C::MUTED, td::top_left, "Last strike %s, %.0f km away", hm, t.lastStrikeKm);
  } else {
    text("No strikes detected", x + 16, y + 80, F::XS, C::DIM);
  }
}

}  // namespace

void pageAlerts() {
  warningsCard(DataService::warnings());
  auroraCard(DataService::aurora());
  lightningCard(DataService::tempest());
}

}  // namespace UiDraw
