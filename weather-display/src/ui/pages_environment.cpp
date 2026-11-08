/**
 * @file    pages_environment.cpp
 * @brief   Air-quality and Met Office warnings pages.
 */
#include "config.h"
#include "connectivity.h"
#include "theme.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

/** @brief Colour for an EAQI band index (Good -> Extremely poor). */
uint16_t aqiColor(uint8_t bandIdx) {
  static const uint16_t colors[] = {0x07FB /*teal*/, Theme::GREEN, Theme::YELLOW, Theme::ORANGE, Theme::RED, 0xA01F /*purple*/};
  return colors[min<uint8_t>(bandIdx, 5)];
}

}  // namespace

/** @brief Big EAQI value with band, colour scale, pollutants, UV and pollen. */
void pageAir(const UiModel &m) {
  const AirQualityData &aq = m.air;
  if (!aq.valid) return waiting("Fetching air quality...");
  char buf[32];
  const uint16_t col = aqiColor(AirQuality::bandIndex(aq.eaqi));

  snprintf(buf, sizeof(buf), "%d", aq.eaqi);
  text(buf, 4, 24, 4, col);
  text(AirQuality::band(aq.eaqi), 68, 24, 2, col);
  text("European AQI", 68, 42, 1, Theme::GREY);

  // Six-segment colour scale with a marker at the current value (0..120).
  constexpr int sx = 68, sw = 168, sy = 53;
  for (int i = 0; i < 6; ++i) canvas.fillRect(sx + i * sw / 6, sy, sw / 6 - 1, 5, aqiColor(i));
  const int mx = sx + min(aq.eaqi, 119) * sw / 120;
  canvas.fillTriangle(mx, sy - 1, mx - 3, sy - 5, mx + 3, sy - 5, Theme::WHITE);

  snprintf(buf, sizeof(buf), "PM2.5 %-3d PM10 %d", static_cast<int>(lroundf(aq.pm25)), static_cast<int>(lroundf(aq.pm10)));
  text(buf, 4, 66, 2, Theme::WHITE);
  snprintf(buf, sizeof(buf), "NO2   %-3d O3   %d", static_cast<int>(lroundf(aq.no2)), static_cast<int>(lroundf(aq.o3)));
  text(buf, 4, 85, 2, Theme::LIGHT);

  const float uvMax = m.weather.dailyCount ? m.weather.daily[0].uvMax : aq.uv;
  snprintf(buf, sizeof(buf), "UV %d (max %d %s)", static_cast<int>(lroundf(aq.uv)), static_cast<int>(lroundf(uvMax)),
           AirQuality::uvBand(uvMax));
  text(buf, 4, 106, 1, Theme::YELLOW);
  const char *ptype = nullptr;
  const char *plevel = AirQuality::pollen(aq, ptype);
  snprintf(buf, sizeof(buf), "Pollen %s%s%s", plevel, ptype ? " " : "", ptype ? ptype : "");
  text(buf, 4, 118, 1, Theme::GREEN);
}

/** @brief Up to three warnings as coloured cards, or an all-clear message. */
namespace {

constexpr int AURORA_Y = 84;      ///< Top of the aurora section.
constexpr int CHART_X = 143;      ///< Left edge of the 24-hour activity chart.
constexpr int CHART_BOTTOM = 120; ///< Baseline of the activity chart.
constexpr int CHART_H = 34;       ///< Chart height; 200 nT (red threshold) fills it.

/** @brief Met Office section (y 21..78): all-clear line or up to two warning cards. */
void warningsSection(const WarningList &wl) {
  if (!wl.valid) {
    text("Checking Met Office...", 4, 30, 1, Theme::LIGHT);
    return;
  }
  if (wl.count == 0) {
    canvas.fillCircle(14, 34, 9, Theme::GREEN);
    canvas.drawLine(9, 34, 12, 38, Theme::BG);
    canvas.drawLine(12, 38, 19, 29, Theme::BG);
    text("No warnings", 30, 24, 2, Theme::WHITE);
    text("Met Office - " WARNING_AREA_FILTER, 30, 44, 1, Theme::GREY);
    return;
  }

  char buf[40];
  int y = 21;
  const int shown = min<int>(wl.count, 2);
  for (int i = 0; i < shown; ++i, y += 29) {
    const WeatherWarning &w = wl.items[i];
    const uint16_t col = warningColor(w.level);
    canvas.fillRect(0, y, 4, 26, col);
    warningIcon(8, y + 2, w.level);
    snprintf(buf, sizeof(buf), "%s %s", Warnings::levelName(w.level), w.hazard);
    const bool fitsLarge = strlen(buf) <= 17;  // (240 - 26) / 12 px per char.
    text(buf, 26, fitsLarge ? y : y + 4, fitsLarge ? 2 : 1, col);
    snprintf(buf, sizeof(buf), "%.38s", w.validity);
    text(buf, 8, y + 17, 1, Theme::LIGHT);
  }
  if (wl.total > shown) {
    snprintf(buf, sizeof(buf), "+%d more", wl.total - shown);
    rightAligned(buf, W - 2, 21, 1, Theme::GREY);
  }
}

/** @brief AuroraWatch section (y 84..122): level, meaning and 24-hour activity bars. */
void auroraSection(const AuroraData &a) {
  auroraIcon(2, AURORA_Y, a.valid ? a.level : AuroraLevel::Unknown);
  text("AuroraWatch UK", 18, AURORA_Y + 2, 1, Theme::GREY);
  if (!a.valid) {
    text("Checking...", 4, AURORA_Y + 16, 1, Theme::LIGHT);
    return;
  }

  char buf[16];
  const uint16_t col = auroraColor(a.level);
  text(Aurora::levelName(a.level), 4, AURORA_Y + 13, 2, col);
  snprintf(buf, sizeof(buf), "%.0fnT", Aurora::latestActivity(a));
  text(buf, 4 + strlen(Aurora::levelName(a.level)) * 12 + 6, AURORA_Y + 19, 1, Theme::LIGHT);
  text(Aurora::meaning(a.level), 4, AURORA_Y + 31, 1, a.level >= AuroraLevel::Amber ? col : Theme::LIGHT);

  // 24 hourly bars (oldest left), coloured by that hour's level; 200 nT fills the chart.
  for (int i = 0; i < a.count; ++i) {
    const int h = constrain(static_cast<int>(a.activity[i] / 200.0f * CHART_H), 1, CHART_H);
    canvas.fillRect(CHART_X + i * 4, CHART_BOTTOM - h, 3, h, auroraColor(a.hourLevel[i]));
  }
  // Amber threshold (100 nT) as a dotted guide line.
  for (int x = CHART_X; x < CHART_X + 96; x += 3) canvas.drawPixel(x, CHART_BOTTOM - CHART_H / 2, Theme::ORANGE);
  canvas.drawFastHLine(CHART_X, CHART_BOTTOM, 96, Theme::GREY);
  rightAligned("24h", W - 1, AURORA_Y, 1, Theme::GREY);
}

}  // namespace

/** @brief Met Office warnings (top) and AuroraWatch UK status (bottom). */
void pageAlerts(const UiModel &m) {
  warningsSection(m.warnings);
  canvas.drawFastHLine(0, AURORA_Y - 4, W, Theme::DKGREY);
  auroraSection(m.aurora);
}

}  // namespace UiDraw
