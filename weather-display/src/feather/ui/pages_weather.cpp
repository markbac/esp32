/**
 * @file    pages_weather.cpp
 * @brief   Now, Hourly and Daily forecast pages.
 */
#include "connectivity.h"
#include "icons.h"
#include "theme.h"
#include "ui_draw.h"

namespace UiDraw {

/** @brief Large icon and temperature with the main current-condition details. */
void pageNow(const UiModel &m) {
  const WeatherData &wx = m.weather;
  if (!wx.valid) return waiting("Fetching weather...");
  const CurrentWeather &c = wx.current;

  Icons::draw(canvas, 28, 44, 48, c.code, c.isDay);

  // Big temperature with degree ring and "C".
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", static_cast<int>(lroundf(c.temp)));
  text(buf, 58, 24, 5, Theme::WHITE);
  const int x = canvas.getCursorX();
  degree(x + 2, 24, 4, Theme::WHITE);
  text("C", x + 14, 24, 3, Theme::WHITE);

  // Today's high / low.
  if (wx.dailyCount > 0) {
    snprintf(buf, sizeof(buf), "H%3d", static_cast<int>(lroundf(wx.daily[0].tMax)));
    text(buf, 180, 26, 2, Theme::ORANGE);
    snprintf(buf, sizeof(buf), "L%3d", static_cast<int>(lroundf(wx.daily[0].tMin)));
    text(buf, 180, 46, 2, Theme::CYAN);
  }

  text(Weather::describe(c.code), 4, 70, 2, Theme::YELLOW);

  snprintf(buf, sizeof(buf), "Feels %d  Hum %d%%", static_cast<int>(lroundf(c.feelsLike)), c.humidity);
  text(buf, 4, 92, 2, Theme::WHITE);

  snprintf(buf, sizeof(buf), "%dmph %s", static_cast<int>(lroundf(c.wind)), Weather::compass(c.windDir));
  text(buf, 4, 114, 2, Theme::WHITE);

  // Bottom-right: "rain soon" from the nowcast beats the daily chance.
  tm t;
  const RainSoon rs = Net::localTime(t) ? Weather::rainSoon(wx, t.tm_hour * 60 + t.tm_min) : RainSoon{};
  if (rs.state == RainSoon::State::Raining) {
    snprintf(buf, sizeof(buf), "Raining");
  } else if (rs.state == RainSoon::State::Starting) {
    snprintf(buf, sizeof(buf), "Rain%3dm", rs.minutes);
  } else if (wx.dailyCount > 0) {
    snprintf(buf, sizeof(buf), "Rain%3d%%", wx.daily[0].rainChance);
  } else {
    buf[0] = '\0';
  }
  rightAligned(buf, W - 4, 114, 2, Theme::BLUE);
}

/** @brief Columns of time / icon / temp / rain % / wind for the coming hours. */
void pageHourly(const UiModel &m) {
  const WeatherData &wx = m.weather;
  if (!wx.valid) return waiting("Fetching weather...");
  constexpr int colW = W / HOURLY_COLUMNS;
  char buf[12];
  int col = 0;
  // Index 0 is the current hour, so start from the next sample.
  for (int i = HOURLY_STEP; i < wx.hourlyCount && col < HOURLY_COLUMNS; i += HOURLY_STEP, ++col) {
    const HourlyPoint &h = wx.hourly[i];
    const int cx = col * colW + colW / 2;
    snprintf(buf, sizeof(buf), "%02u:00", h.hour);
    centered(buf, cx, 22, 1, Theme::LIGHT);
    Icons::draw(canvas, cx, 47, 30, h.code, h.isDay);
    tempCentered(h.temp, cx, 66, 2, Theme::WHITE);
    snprintf(buf, sizeof(buf), "%d%%", h.rainChance);
    centered(buf, cx, 88, 1, Theme::BLUE);
    snprintf(buf, sizeof(buf), "%dmph", static_cast<int>(lroundf(h.wind)));
    centered(buf, cx, 100, 1, Theme::GREY);
  }
  for (int i = 1; i < HOURLY_COLUMNS; ++i) canvas.drawFastVLine(i * colW, 24, 84, Theme::DKGREY);
  snprintf(buf, sizeof(buf), "Next %dh", HOURLY_COLUMNS * HOURLY_STEP);
  centered(buf, W / 2, 116, 1, Theme::GREY);
}

/** @brief One column per day: weekday, icon, high, low, rain %. */
void pageDaily(const UiModel &m) {
  const WeatherData &wx = m.weather;
  if (!wx.valid) return waiting("Fetching weather...");
  const int colW = W / max<int>(1, wx.dailyCount);
  char buf[8];
  for (int i = 0; i < wx.dailyCount; ++i) {
    const DailyPoint &d = wx.daily[i];
    const int cx = i * colW + colW / 2;
    centered(i == 0 ? "Today" : WEEKDAYS[d.weekday % 7], cx, 22, i == 0 ? 1 : 2, i == 0 ? Theme::YELLOW : Theme::WHITE);
    Icons::draw(canvas, cx, 55, 34, d.code, true);
    tempCentered(d.tMax, cx, 76, 2, Theme::ORANGE);
    tempCentered(d.tMin, cx, 96, 2, Theme::CYAN);
    snprintf(buf, sizeof(buf), "%d%%", d.rainChance);
    centered(buf, cx, 116, 1, Theme::BLUE);
    if (i > 0) canvas.drawFastVLine(i * colW, 24, 100, Theme::DKGREY);
  }
}

}  // namespace UiDraw
