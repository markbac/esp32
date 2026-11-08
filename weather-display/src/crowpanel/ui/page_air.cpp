/**
 * @file    page_air.cpp
 * @brief   "Air" page: European AQI, pollutants against WHO guidelines, UV and pollen.
 */
#include "connectivity.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

/** @brief One pollutant row: name, value and a bar scaled to its WHO 24-h guideline. */
void pollutant(int x, int y, int w, const char *name, float value, float guideline) {
  text(name, x, y, F::S, C::TEXT);
  textf(x + 150, y, F::S, C::TEXT, td::top_right, "%.0f", value);
  const float ratio = value / guideline;
  const uint16_t col = ratio < 0.5f ? C::GREEN : ratio < 1.0f ? C::YELLOW : ratio < 2.0f ? C::ORANGE : C::RED;
  const int bx = x + 166, bw = w - 166;
  canvas.fillRoundRect(bx, y + 4, bw, 14, 7, C::CARD2);
  canvas.fillRoundRect(bx, y + 4, max(14, static_cast<int>(min(ratio / 2.0f, 1.0f) * bw)), 14, 7, col);
  canvas.drawFastVLine(bx + bw / 2, y, 22, C::MUTED);  // WHO guideline at the middle.
}

}  // namespace

void pageAir() {
  const AirQualityData &aq = DataService::air();
  if (!aq.valid) {
    placeholder(0, BODY_Y, W, BODY_H, Net::connected() ? "Fetching air quality..." : "Connecting Wi-Fi...");
    return;
  }
  const uint8_t band = AirQuality::bandIndex(aq.eaqi);
  const uint16_t col = aqiColor(band);

  // AQI card.
  {
    constexpr int x = M, y = BODY_Y + M, w = 384, h = 236;
    card(x, y, w, h, "European Air Quality Index");
    textf(x + 20, y + 36, F::XXL, col, td::top_left, "%d", aq.eaqi);
    text(AirQuality::band(aq.eaqi), x + 20, y + 118, F::M, col);
    const int sx = x + 20, sw = w - 40, sy = y + 168;
    for (int i = 0; i < 6; ++i) canvas.fillRect(sx + i * sw / 6, sy, sw / 6 - 2, 12, aqiColor(i));
    const int mx = sx + min(aq.eaqi, 119) * sw / 120;
    canvas.fillTriangle(mx, sy - 2, mx - 7, sy - 12, mx + 7, sy - 12, C::TEXT);
    text("Good", sx, sy + 18, F::XS, C::MUTED);
    text("Extremely poor", sx + sw, sy + 18, F::XS, C::MUTED, td::top_right);
  }
  // Pollutants card.
  {
    constexpr int x = 400, y = BODY_Y + M, w = 392, h = 236;
    card(x, y, w, h, "Pollutants (ug/m3)");
    pollutant(x + 16, y + 48, w - 32, "PM2.5", aq.pm25, 15);
    pollutant(x + 16, y + 92, w - 32, "PM10", aq.pm10, 45);
    pollutant(x + 16, y + 136, w - 32, "NO2", aq.no2, 25);
    pollutant(x + 16, y + 180, w - 32, "Ozone", aq.o3, 100);
    text("| = WHO 24-hour guideline", x + w - 16, y + h - 22, F::XS, C::DIM, td::top_right);
  }
  // UV card.
  {
    constexpr int x = M, y = BODY_Y + 252, w = 384, h = 120;
    card(x, y, w, h, "UV index");
    const TempestData &t = DataService::tempest();
    const float uvNow = t.fresh() ? t.obs.uv : aq.uv;
    const float uvMax = DataService::weather().dailyCount ? DataService::weather().daily[0].uvMax : uvNow;
    textf(x + 20, y + 40, F::L, C::YELLOW, td::top_left, "%.1f", uvNow);
    textf(x + 130, y + 44, F::S, C::TEXT, td::top_left, "now (%s)", t.fresh() ? "Tempest" : "model");
    textf(x + 130, y + 72, F::S, C::MUTED, td::top_left, "max today %.0f, %s", uvMax, AirQuality::uvBand(uvMax));
  }
  // Pollen card.
  {
    constexpr int x = 400, y = BODY_Y + 252, w = 392, h = 120;
    card(x, y, w, h, "Pollen");
    const char *type = nullptr;
    const char *level = AirQuality::pollen(aq, type);
    textf(x + 20, y + 40, F::M, C::GREEN, td::top_left, "%s%s%s", level, type ? " - " : "", type ? type : "");
    textf(x + 20, y + 80, F::XS, C::MUTED, td::top_left, "Grass %.0f   Birch %.0f   Alder %.0f grains/m3", aq.grass,
          aq.birch, aq.alder);
  }
}

}  // namespace UiDraw
