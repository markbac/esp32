/**
 * @file    page_sky.cpp
 * @brief   Sun (arc, sunrise, sunset, day length) and moon (phase, next full/new).
 *
 * @details
 * @verbatim
 *   +-----------------+-------------------+
 *   |    .  o  .      |       (moon)      |
 *   |  .         .    |   Waxing gibbous  |
 *   | ---------------  |      78% lit      |
 *   | ^07:21   v16:24 |  Full  Sat 14 Nov |
 *   |  Day 9h03m      |  New   Sun 29 Nov |
 *   +-----------------+-------------------+
 * @endverbatim
 */
#include "astro.h"
#include "connectivity.h"
#include "icons.h"
#include "theme.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

constexpr int SUN_CX = 60;      ///< Centre of the sun arc.
constexpr int HORIZON_Y = 66;   ///< Horizon line (arc baseline).
constexpr int ARC_R = 42;       ///< Arc radius.
constexpr int MOON_CX = 180;    ///< Moon centre x.
constexpr int MOON_CY = 44;     ///< Moon centre y.

/** @brief Format "Ddd DD Mon" for an epoch into @p buf (12 bytes). */
void shortDate(time_t t, char *buf) {
  tm lt;
  localtime_r(&t, &lt);
  strftime(buf, 12, "%a %d %b", &lt);
}

/** @brief Dotted day arc, horizon, current sun position and rise/set times. */
void sunPanel(const WeatherData &wx) {
  // Dotted semicircle.
  for (int deg = 0; deg <= 180; deg += 10) {
    const float a = deg * DEG_TO_RAD;
    canvas.fillCircle(SUN_CX + lroundf(cosf(a) * ARC_R), HORIZON_Y - lroundf(sinf(a) * ARC_R), 1, Theme::DKGREY);
  }
  canvas.drawFastHLine(4, HORIZON_Y, 112, Theme::GREY);

  const int rise = Astro::minutesOfDay(wx.sunrise);
  const int set = Astro::minutesOfDay(wx.sunset);
  tm t;
  if (rise >= 0 && set > rise && Net::localTime(t)) {
    const int now = t.tm_hour * 60 + t.tm_min;
    if (now >= rise && now <= set) {
      const float frac = static_cast<float>(now - rise) / (set - rise);
      const float a = PI * (1.0f - frac);
      const int sx = SUN_CX + lroundf(cosf(a) * ARC_R), sy = HORIZON_Y - lroundf(sinf(a) * ARC_R);
      canvas.fillCircle(sx, sy, 6, Theme::YELLOW);
    } else {
      centered("night", SUN_CX, HORIZON_Y - 20, 1, Theme::GREY);
    }
  }

  Icons::sunEvent(canvas, 22, HORIZON_Y + 12, 22, true);
  Icons::sunEvent(canvas, 98, HORIZON_Y + 12, 22, false);
  centered(wx.sunrise, 22, HORIZON_Y + 25, 1, Theme::WHITE);
  centered(wx.sunset, 98, HORIZON_Y + 25, 1, Theme::WHITE);

  char buf[24];
  const int dayMin = static_cast<int>(wx.daylightSec / 60);
  snprintf(buf, sizeof(buf), "Day %dh%02dm", dayMin / 60, dayMin % 60);
  centered(buf, SUN_CX, HORIZON_Y + 38, 1, Theme::LIGHT);
  snprintf(buf, sizeof(buf), "Sunshine %.1fh", wx.sunshineSec / 3600.0f);
  centered(buf, SUN_CX, HORIZON_Y + 50, 1, Theme::YELLOW);
}

/** @brief Moon picture, phase name, illumination and next full/new dates. */
void moonPanel() {
  const time_t now = time(nullptr);
  const MoonInfo moon = Astro::moon(now);
  Icons::moon(canvas, MOON_CX, MOON_CY, 22, moon.phase);

  char buf[24], date[12];
  centered(moon.name, MOON_CX, 72, 1, Theme::WHITE);
  snprintf(buf, sizeof(buf), "%d%% lit", static_cast<int>(lroundf(moon.illumination * 100)));
  centered(buf, MOON_CX, 84, 1, Theme::LIGHT);
  shortDate(moon.nextFull, date);
  snprintf(buf, sizeof(buf), "Full %s", date);
  centered(buf, MOON_CX, 100, 1, Theme::YELLOW);
  shortDate(moon.nextNew, date);
  snprintf(buf, sizeof(buf), "New  %s", date);
  centered(buf, MOON_CX, 112, 1, Theme::GREY);
}

}  // namespace

void pageSky(const UiModel &m) {
  if (!m.weather.valid || !Net::timeValid()) return waiting("Fetching sun times...");
  sunPanel(m.weather);
  canvas.drawFastVLine(120, 22, 102, Theme::DKGREY);
  moonPanel();
}

}  // namespace UiDraw
