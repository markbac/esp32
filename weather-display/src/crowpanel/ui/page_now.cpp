/**
 * @file    page_now.cpp
 * @brief   "Now" page: live Tempest readings, wind compass and short outlook.
 *
 * @details
 * @verbatim
 *   +-- Outside (Tempest) -----------+ +-- Wind ---------------------------+
 *   |  8.4°            Humidity 91%  | |      N          Avg     3.1 mph   |
 *   |  Feels 6.1°      Dew pt  6.9°  | |   W  (4)  E     Gust    5.2 mph   |
 *   |  H 11°  L 5°     Pressure ...  | |      S          Lull    0.8 mph   |
 *   |                  Rain today .. | |   live needle   From SW 225°      |
 *   |  Tempest - 32 s ago   UV / ⚡   | |                 Live   2.1 mph    |
 *   +--------------------------------+ +-----------------------------------+
 *   +-- Outlook -------------------------------------------------------------+
 *   | [icon] Overcast   Rain: dry for 2 h   [AQI 43] [sun]  next 8 hours ... |
 *   +------------------------------------------------------------------------+
 * @endverbatim
 *          Falls back to Open-Meteo values when no Tempest data is fresh.
 */
#include "astro.h"
#include "connectivity.h"
#include "icons.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

/** @brief Values for the "Outside" card from Tempest, or Open-Meteo as fallback. */
struct Outside {
  bool have = false;
  const char *source = "";
  float temp = NAN, feels = NAN, humidity = NAN, dew = NAN, pressure = NAN;
  float rainToday = NAN, uv = NAN, solar = NAN;
  float windAvg = NAN, windGust = NAN, windLull = NAN, liveMph = NAN;
  int windDir = -1, liveDir = -1;
  uint32_t updatedMs = 0;
};

/** @brief Magnus dew point (for the Open-Meteo fallback). */
float dewPoint(float t, float rh) {
  const float a = 17.625f, b = 243.04f, g = logf(rh / 100.0f) + a * t / (b + t);
  return b * g / (a - g);
}

Outside gather() {
  Outside o;
  const TempestData &t = DataService::tempest();
  const WeatherData &wx = DataService::weather();
  if (t.fresh()) {
    o.have = true;
    o.source = "Tempest";
    o.temp = t.obs.tempC;
    o.feels = t.feelsC;
    o.humidity = t.obs.humidity;
    o.dew = t.dewPointC;
    o.pressure = t.obs.pressureSea;
    o.rainToday = t.rainTodayMm;
    o.uv = t.obs.uv;
    o.solar = t.obs.solar;
    o.windAvg = t.obs.windAvg;
    o.windGust = t.obs.windGust;
    o.windLull = t.obs.windLull;
    o.windDir = t.obs.windDir;
    if (t.rapidValid && millis() - t.rapidAtMs < 30000) {
      o.liveMph = t.rapidMph;
      o.liveDir = t.rapidDir;
    }
    o.updatedMs = t.obsAtMs;
  } else if (wx.valid) {
    const CurrentWeather &c = wx.current;
    o.have = true;
    o.source = "Open-Meteo";
    o.temp = c.temp;
    o.feels = c.feelsLike;
    o.humidity = c.humidity;
    o.dew = dewPoint(c.temp, c.humidity);
    o.pressure = c.pressure;
    o.windAvg = c.wind;
    o.windGust = c.gust;
    o.windDir = c.windDir;
    o.updatedMs = wx.fetchedAtMs;
  }
  return o;
}

/** @brief Label/value row in the right half of the Outside card. */
void row(int y, const char *label, const char *value, uint16_t color = C::TEXT) {
  text(label, 236, y, F::XS, C::MUTED);
  text(value, 384, y + 14, F::S, color, td::top_right);
}

void outsideCard(const Outside &o) {
  constexpr int x = M, y = BODY_Y + M, w = 392, h = 236;
  card(x, y, w, h, "Outside");
  if (!o.have) return placeholder(x, y, w, h, "Waiting for Tempest or forecast...");

  temp(o.temp, x + 16, y + 34, F::HUGE, C::TEXT, td::top_left, 1);
  textf(x + 16, y + 150, F::S, C::MUTED, td::top_left, "Feels");
  temp(o.feels, x + 80, y + 146, F::M, C::TEXT, td::top_left, 1);

  const WeatherData &wx = DataService::weather();
  if (wx.valid && wx.dailyCount) {
    text("High", x + 16, y + 186, F::XS, C::MUTED);
    temp(wx.daily[0].tMax, x + 16, y + 202, F::S, C::ORANGE);
    text("Low", x + 92, y + 186, F::XS, C::MUTED);
    temp(wx.daily[0].tMin, x + 92, y + 202, F::S, C::CYAN);
  }

  char buf[32];
  constexpr int STEP = 38;
  int ry = y + 30;
  snprintf(buf, sizeof(buf), "%.0f%%", o.humidity);
  row(ry, "Humidity", buf);
  ry += STEP;
  snprintf(buf, sizeof(buf), "%.1f C", o.dew);
  row(ry, "Dew point", buf);
  ry += STEP;

  // Pressure with trend arrow (Tempest only).
  uint16_t mins = 0;
  const float trend = DataService::tempest().fresh() ? DataService::tempest().pressureTrend(mins) : NAN;
  snprintf(buf, sizeof(buf), "%.1f hPa", o.pressure);
  row(ry, "Pressure", buf);
  if (!isnan(trend)) {
    const int ax = 236 + 74, ay = ry + 6;
    const uint16_t col = fabsf(trend) < 0.5f ? C::MUTED : (trend > 0 ? C::GREEN : C::ORANGE);
    if (fabsf(trend) < 0.5f) canvas.fillTriangle(ax, ay - 4, ax, ay + 4, ax + 8, ay, col);
    else if (trend > 0) canvas.fillTriangle(ax, ay + 5, ax + 8, ay + 5, ax + 4, ay - 4, col);
    else canvas.fillTriangle(ax, ay - 4, ax + 8, ay - 4, ax + 4, ay + 5, col);
    textf(ax + 12, ry, F::XS, col, td::top_left, "%+.1f/%uh", trend, max<unsigned>(1, (mins + 30) / 60));
  }
  ry += STEP;
  if (!isnan(o.rainToday)) snprintf(buf, sizeof(buf), "%.1f mm", o.rainToday);
  else snprintf(buf, sizeof(buf), "--");
  row(ry, "Rain today", buf, C::BLUE);
  ry += STEP;
  if (!isnan(o.uv)) snprintf(buf, sizeof(buf), "%.1f / %.0f W", o.uv, o.solar);
  else snprintf(buf, sizeof(buf), "--");
  row(ry, "UV / solar", buf, C::YELLOW);

  char when[16];
  ago(o.updatedMs, when, sizeof(when));
  textf(x + w - 14, y + 12, F::XS, C::DIM, td::top_right, "%s, %s", o.source, when);
}

/** @brief Compass dial with average-direction marker and live needle. */
void windCard(const Outside &o) {
  constexpr int x = 408, y = BODY_Y + M, w = 384, h = 236;
  card(x, y, w, h, "Wind");
  if (!o.have) return placeholder(x, y, w, h, "No wind data");

  const int cx = x + 120, cy = y + 128, r = 92;
  canvas.fillCircle(cx, cy, r, C::CARD2);
  canvas.drawCircle(cx, cy, r, C::LINE);
  for (int deg = 0; deg < 360; deg += 15) {
    const float a = deg * DEG_TO_RAD;
    const int len = deg % 90 == 0 ? 12 : (deg % 45 == 0 ? 8 : 4);
    canvas.drawLine(cx + sinf(a) * (r - len), cy - cosf(a) * (r - len), cx + sinf(a) * r, cy - cosf(a) * r, C::MUTED);
  }
  const char *cardinal[] = {"N", "E", "S", "W"};
  for (int i = 0; i < 4; ++i) {
    const float a = i * 90 * DEG_TO_RAD;
    text(cardinal[i], cx + sinf(a) * (r - 26), cy - cosf(a) * (r - 26), F::S, i == 0 ? C::RED : C::TEXT,
         td::middle_center);
  }

  const bool calm = o.windAvg < 0.5f && (isnan(o.liveMph) || o.liveMph < 0.5f);
  // Average direction: wedge on the rim pointing inwards (wind comes FROM here).
  if (o.windDir >= 0 && !calm) {
    const float a = o.windDir * DEG_TO_RAD;
    const float tx = sinf(a), ty = -cosf(a), nx = cosf(a), ny = sinf(a);
    canvas.fillTriangle(cx + tx * (r + 2) + nx * 10, cy + ty * (r + 2) + ny * 10, cx + tx * (r + 2) - nx * 10,
                        cy + ty * (r + 2) - ny * 10, cx + tx * (r - 20), cy + ty * (r - 20), C::ACCENT);
  }
  // Live needle (3-second wind), pointing downwind through the centre.
  if (o.liveDir >= 0 && o.liveMph >= 0.5f) {
    const float a = o.liveDir * DEG_TO_RAD;
    canvas.drawWideLine(cx + sinf(a) * (r - 30), cy - cosf(a) * (r - 30), cx - sinf(a) * (r - 30), cy + cosf(a) * (r - 30),
                        2.5f, C::YELLOW);
  }
  canvas.fillCircle(cx, cy, 34, C::CARD);
  const float speed = !isnan(o.liveMph) ? o.liveMph : o.windAvg;
  if (calm) {
    text("Calm", cx, cy, F::S, C::MUTED, td::middle_center);
  } else {
    textf(cx, cy - 6, F::L, C::TEXT, td::middle_center, "%.0f", speed);
    text("mph", cx, cy + 20, F::XS, C::MUTED, td::middle_center);
  }

  // Figures on the right.
  const int fx = x + 236;
  char buf[24];
  auto fig = [&](int fy, const char *label, const char *val, uint16_t col) {
    text(label, fx, fy, F::XS, C::MUTED);
    text(val, fx, fy + 15, F::S, col);
  };
  snprintf(buf, sizeof(buf), "%.1f mph", o.windAvg);
  fig(y + 34, "Average", buf, C::TEXT);
  snprintf(buf, sizeof(buf), "%.1f mph", o.windGust);
  fig(y + 74, "Gust", buf, C::ORANGE);
  if (!isnan(o.windLull)) {
    snprintf(buf, sizeof(buf), "%.1f mph", o.windLull);
    fig(y + 114, "Lull", buf, C::MUTED);
  }
  if (calm) snprintf(buf, sizeof(buf), "--");
  else snprintf(buf, sizeof(buf), "%s %d", Weather::compass(o.windDir), o.windDir);
  fig(y + 154, "From", buf, C::ACCENT);
  if (!isnan(o.liveMph)) {
    snprintf(buf, sizeof(buf), "%.1f mph", o.liveMph);
    fig(y + 194, "Live (3 s)", buf, C::YELLOW);
  }
}

/** @brief Condition, rain-soon text, chips and the next 8 hours. */
void outlookCard() {
  constexpr int x = M, y = BODY_Y + 252, w = W - 2 * M, h = 120;
  card(x, y, w, h);
  const WeatherData &wx = DataService::weather();
  if (!wx.valid) return placeholder(x, y, w, h, Net::connected() ? "Fetching forecast..." : "Connecting Wi-Fi...");

  Icons::draw(canvas, x + 56, y + 58, 92, wx.current.code, wx.current.isDay, C::CARD);
  text(Weather::describe(wx.current.code), x + 116, y + 12, F::M, C::TEXT);

  tm t;
  const RainSoon rs = Net::localTime(t) ? Weather::rainSoon(wx, t.tm_hour * 60 + t.tm_min) : RainSoon{};
  char buf[40];
  uint16_t col = C::GREEN;
  switch (rs.state) {
    case RainSoon::State::Dry: snprintf(buf, sizeof(buf), "Dry for the next 2 hours"); break;
    case RainSoon::State::Starting:
      snprintf(buf, sizeof(buf), "Rain starting in ~%d min", rs.minutes);
      col = C::BLUE;
      break;
    case RainSoon::State::Raining:
      if (rs.minutes >= 0) snprintf(buf, sizeof(buf), "Raining, easing in ~%d min", rs.minutes);
      else snprintf(buf, sizeof(buf), "Raining for 2 h+");
      col = C::BLUE;
      break;
    default: snprintf(buf, sizeof(buf), "Rain outlook unavailable"); col = C::DIM; break;
  }
  text(buf, x + 116, y + 46, F::S, col);

  int cx = x + 116;
  const AirQualityData &aq = DataService::air();
  if (aq.valid) {
    snprintf(buf, sizeof(buf), "AQI %d %s", aq.eaqi, AirQuality::band(aq.eaqi));
    cx += chip(cx, y + 78, buf, aqiColor(AirQuality::bandIndex(aq.eaqi)), C::BG, F::XS) + 8;
  }
  snprintf(buf, sizeof(buf), "Sun %s - %s", wx.sunrise, wx.sunset);
  chip(cx, y + 78, buf, C::CARD2, C::YELLOW, F::XS);

  // Next 8 hours.
  const int hx = x + 440, colW = (w - 440 - 8) / 8;
  canvas.drawFastVLine(hx - 8, y + 12, h - 24, C::LINE);
  for (int i = 1, col2 = 0; i < wx.hourlyCount && col2 < 8; ++i, ++col2) {
    const HourlyPoint &p = wx.hourly[i];
    const int ccx = hx + col2 * colW + colW / 2;
    textf(ccx, y + 10, F::XS, C::MUTED, td::top_center, "%02u", p.hour);
    Icons::draw(canvas, ccx, y + 50, 34, p.code, p.isDay, C::CARD);
    temp(p.temp, ccx, y + 74, F::S, C::TEXT, td::top_center);
    if (p.rainChance >= 20) textf(ccx, y + 98, F::XS, C::BLUE, td::top_center, "%d%%", p.rainChance);
  }
}

}  // namespace

void pageNow() {
  const Outside o = gather();
  outsideCard(o);
  windCard(o);
  outlookCard();
}

}  // namespace UiDraw
