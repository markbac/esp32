/**
 * @file    page_sky.cpp
 * @brief   "Sky" page: sun arc with position, day length, and the moon phase.
 */
#include "astro.h"
#include "connectivity.h"
#include "icons.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

void shortDate(time_t t, char *buf, size_t n) {
  tm lt;
  localtime_r(&t, &lt);
  strftime(buf, n, "%a %d %b", &lt);
}

void sunCard(const WeatherData &wx) {
  constexpr int x = M, y = BODY_Y + M, w = 516, h = BODY_H - 2 * M;
  card(x, y, w, h, "Sun");
  const int cx = x + w / 2, base = y + 256, r = 190;

  // Arc (dotted) and horizon.
  for (int deg = 0; deg <= 180; deg += 5) {
    const float a = deg * DEG_TO_RAD;
    canvas.fillCircle(cx + cosf(a) * r, base - sinf(a) * r, 2, C::LINE);
  }
  canvas.drawFastHLine(x + 16, base, w - 32, C::MUTED);

  const int rise = Astro::minutesOfDay(wx.sunrise), set = Astro::minutesOfDay(wx.sunset);
  tm t;
  if (rise >= 0 && set > rise && Net::localTime(t)) {
    const int now = t.tm_hour * 60 + t.tm_min;
    if (now >= rise && now <= set) {
      const float a = PI * (1.0f - static_cast<float>(now - rise) / (set - rise));
      const int sx = cx + cosf(a) * r, sy = base - sinf(a) * r;
      canvas.fillCircle(sx, sy, 18, C::YELLOW);
      canvas.drawCircle(sx, sy, 24, C::ORANGE);
      const int left = set - now;
      textf(cx, base - 70, F::S, C::MUTED, td::top_center, "Sunset in %dh %02dm", left / 60, left % 60);
    } else {
      const int until = now < rise ? rise - now : rise + 1440 - now;
      text("Night", cx, base - 96, F::M, C::MUTED, td::top_center);
      textf(cx, base - 64, F::S, C::DIM, td::top_center, "Sunrise in %dh %02dm", until / 60, until % 60);
    }
  }

  Icons::sunEvent(canvas, x + 72, base + 30, 56, true, C::CARD);
  Icons::sunEvent(canvas, x + w - 72, base + 30, 56, false, C::CARD);
  text(wx.sunrise, x + 72, base + 52, F::M, C::TEXT, td::top_center);
  text(wx.sunset, x + w - 72, base + 52, F::M, C::TEXT, td::top_center);

  const int day = static_cast<int>(wx.daylightSec / 60);
  textf(cx, base + 20, F::S, C::TEXT, td::top_center, "Daylight %dh %02dm", day / 60, day % 60);
  textf(cx, base + 48, F::S, C::YELLOW, td::top_center, "Sunshine %.1f h", wx.sunshineSec / 3600.0f);
  const TempestData &tp = DataService::tempest();
  if (tp.fresh()) {
    textf(x + 16, y + 40, F::XS, C::MUTED, td::top_left, "Solar %.0f W/m2   %.0f lux", tp.obs.solar, tp.obs.lux);
  }
}

void moonCard() {
  constexpr int x = 532, y = BODY_Y + M, w = W - 532 - M, h = BODY_H - 2 * M;
  card(x, y, w, h, "Moon");
  const time_t now = time(nullptr);
  const MoonInfo m = Astro::moon(now);
  const int cx = x + w / 2;
  Icons::moon(canvas, cx, y + 120, 74, m.phase);
  text(m.name, cx, y + 210, F::M, C::TEXT, td::top_center);
  textf(cx, y + 244, F::S, C::MUTED, td::top_center, "%d%% lit, %.1f days old", static_cast<int>(lroundf(m.illumination * 100)),
        m.ageDays);
  char d[16];
  shortDate(m.nextFull, d, sizeof(d));
  textf(cx, y + 282, F::S, C::YELLOW, td::top_center, "Full  %s", d);
  shortDate(m.nextNew, d, sizeof(d));
  textf(cx, y + 312, F::S, C::MUTED, td::top_center, "New   %s", d);
}

}  // namespace

void pageSky() {
  const WeatherData &wx = DataService::weather();
  if (!wx.valid || !Net::timeValid()) {
    placeholder(0, BODY_Y, W, BODY_H, "Fetching sun times...");
    return;
  }
  sunCard(wx);
  moonCard();
}

}  // namespace UiDraw
