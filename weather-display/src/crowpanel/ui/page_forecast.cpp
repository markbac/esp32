/**
 * @file    page_forecast.cpp
 * @brief   "Forecast" page: 24-hour temperature/rain graph and 7-day outlook.
 */
#include "connectivity.h"
#include "icons.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

/** @brief Line graph of temperature with rain-probability bars and 3-hourly icons. */
void hoursCard(const WeatherData &wx) {
  constexpr int x = M, y = BODY_Y + M, w = W - 2 * M, h = 206;
  card(x, y, w, h, "Next 24 hours");
  const int n = min<int>(wx.hourlyCount - 1, 24);  // Skip index 0 (the current hour).
  if (n < 2) return placeholder(x, y, w, h, "No hourly data");

  const int gx0 = x + 44, gx1 = x + w - 24;  // Plot area.
  const int gTop = y + 78, gBot = y + 166;
  float lo = 1e9, hi = -1e9;
  for (int i = 1; i <= n; ++i) {
    lo = min(lo, wx.hourly[i].temp);
    hi = max(hi, wx.hourly[i].temp);
  }
  if (hi - lo < 4) {
    const float mid = (hi + lo) / 2;
    lo = mid - 2;
    hi = mid + 2;
  }
  auto px = [&](int i) { return gx0 + (i - 1) * (gx1 - gx0) / (n - 1); };
  auto py = [&](float t) { return gBot - static_cast<int>((t - lo) / (hi - lo) * (gBot - gTop)); };

  // Rain probability bars along the bottom.
  const int barW = max(4, (gx1 - gx0) / n - 4);
  for (int i = 1; i <= n; ++i) {
    const int ph = wx.hourly[i].rainChance * 50 / 100;
    if (ph > 0) canvas.fillRect(px(i) - barW / 2, gBot + 22 - ph, barW, ph, rgb(36, 72, 130));
  }
  canvas.drawFastHLine(gx0 - 10, gBot + 22, gx1 - gx0 + 20, C::LINE);

  // Temperature line.
  for (int i = 1; i < n; ++i) {
    canvas.drawWideLine(px(i), py(wx.hourly[i].temp), px(i + 1), py(wx.hourly[i + 1].temp), 1.8f, C::ORANGE);
  }
  // Every third hour: icon, temperature and time.
  for (int i = 1; i <= n; i += 3) {
    const HourlyPoint &p = wx.hourly[i];
    canvas.fillCircle(px(i), py(p.temp), 4, C::ORANGE);
    temp(p.temp, px(i), py(p.temp) - 26, F::XS, C::TEXT, td::top_center);
    Icons::draw(canvas, px(i), y + 50, 34, p.code, p.isDay, C::CARD);
    textf(px(i), gBot + 26, F::XS, C::MUTED, td::top_center, "%02u:00", p.hour);
  }
  text("rain %", gx0 - 40, gBot + 8, F::XS, rgb(70, 120, 190));
}

/** @brief Seven day columns: name, icon, high, low, rain chance, UV. */
void daysCard(const WeatherData &wx) {
  constexpr int x = M, y = BODY_Y + 222, w = W - 2 * M, h = 150;
  card(x, y, w, h);
  if (wx.dailyCount == 0) return placeholder(x, y, w, h, "No daily data");
  const int colW = w / wx.dailyCount;
  for (int i = 0; i < wx.dailyCount; ++i) {
    const DailyPoint &d = wx.daily[i];
    const int cx = x + i * colW + colW / 2;
    if (i > 0) canvas.drawFastVLine(x + i * colW, y + 12, h - 24, C::LINE);
    static const char *const names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    text(i == 0 ? "Today" : names[d.weekday % 7], cx, y + 8, F::S, i == 0 ? C::ACCENT : C::TEXT, td::top_center);
    Icons::draw(canvas, cx, y + 58, 50, d.code, true, C::CARD);
    temp(d.tMax, cx - 22, y + 90, F::M, C::ORANGE, td::top_center);
    temp(d.tMin, cx + 26, y + 96, F::S, C::CYAN, td::top_center);
    textf(cx, y + 126, F::XS, d.rainChance >= 40 ? C::BLUE : C::MUTED, td::top_center, "%d%%  UV %.0f", d.rainChance,
          d.uvMax);
  }
}

}  // namespace

void pageForecast() {
  const WeatherData &wx = DataService::weather();
  if (!wx.valid) {
    placeholder(0, BODY_Y, W, BODY_H, Net::connected() ? "Fetching forecast..." : "Connecting Wi-Fi...");
    return;
  }
  hoursCard(wx);
  daysCard(wx);
}

}  // namespace UiDraw
