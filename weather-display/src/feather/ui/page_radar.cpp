/**
 * @file    page_radar.cpp
 * @brief   Animated rain radar map (left) and 2-hour "rain soon" panel (right).
 *
 * @details
 * @verbatim
 *   +------------------------+-----------+
 *   | 17:20 (frame time)     | Rain 2h   |
 *   |                        | in 25m    |
 *   |     map + rain    (+)  | rain com. |
 *   |                        | ▁▂▅█▅▂    |
 *   | (c)OSM RainViewer      | now   +2h |
 *   | ======-------- progress|           |
 *   +------------------------+-----------+
 * @endverbatim
 *          The loop steps through the last RADAR_FRAMES frames every
 *          RADAR_FRAME_MS, holding the newest frame for RADAR_HOLD_STEPS extra
 *          steps. The frame shown is derived from millis(), so the display and
 *          web screenshots stay in step without extra state.
 */
#include "connectivity.h"
#include "theme.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

constexpr int MAP_Y = 19;                    ///< Top of the map.
constexpr int PANEL_X = RadarLoop::W + 4;    ///< Left edge of the nowcast panel.
constexpr int CHART_Y = 64;                  ///< Top of the bar chart.
constexpr int CHART_H = 36;                  ///< Bar chart height.
constexpr int BAR_W = 9;                     ///< Bar pitch (8 px bar + 1 px gap).

/** @brief Bar colour by rain intensity (mm per 15 minutes). */
uint16_t rainColor(float mm) {
  if (mm < 0.25f) return 0x9E7F;  // Light: pale blue.
  if (mm < 1.0f) return Theme::BLUE;
  return Theme::YELLOW;           // Heavy.
}

/** @brief Index of the frame to show now (oldest = 0). */
int animationFrame(const RadarLoop &r) {
  const uint32_t steps = r.count + RADAR_HOLD_STEPS;
  const int step = static_cast<int>((millis() / RADAR_FRAME_MS) % steps);
  return min(step, r.count - 1);
}

/** @brief Nowcast headline, detail line and bar chart. */
void nowcastPanel(const WeatherData &wx) {
  text("Rain 2h", PANEL_X, 21, 1, Theme::GREY);
  if (!wx.valid || wx.nowcast.count == 0) {
    text("--", PANEL_X, 33, 2, Theme::GREY);
    return;
  }

  tm t;
  const RainSoon rs = Net::localTime(t) ? Weather::rainSoon(wx, t.tm_hour * 60 + t.tm_min) : RainSoon{};
  char head[12] = "--", detail[16] = "";
  uint16_t col = Theme::GREY;
  switch (rs.state) {
    case RainSoon::State::Dry:
      snprintf(head, sizeof(head), "Dry");
      col = Theme::GREEN;
      break;
    case RainSoon::State::Starting:
      snprintf(head, sizeof(head), "in %dm", rs.minutes);
      snprintf(detail, sizeof(detail), "rain coming");
      col = Theme::BLUE;
      break;
    case RainSoon::State::Raining:
      snprintf(head, sizeof(head), "Rain");
      if (rs.minutes >= 0) snprintf(detail, sizeof(detail), "stops ~%dm", rs.minutes);
      else snprintf(detail, sizeof(detail), "for 2h+");
      col = Theme::BLUE;
      break;
    case RainSoon::State::Unknown:
      break;
  }
  text(head, PANEL_X, 33, 2, col);
  text(detail, PANEL_X, 52, 1, Theme::LIGHT);

  // Bars: height ~ sqrt(mm) so drizzle is still visible; 2 mm per 15 min fills.
  for (int i = 0; i < wx.nowcast.count; ++i) {
    const float mm = wx.nowcast.mm[i];
    if (mm < 0.01f) continue;
    const int h = constrain(static_cast<int>(sqrtf(mm / 2.0f) * CHART_H), 2, CHART_H);
    canvas.fillRect(PANEL_X + i * BAR_W, CHART_Y + CHART_H - h, BAR_W - 1, h, rainColor(mm));
  }
  canvas.drawFastHLine(PANEL_X, CHART_Y + CHART_H, BAR_W * NOWCAST_SLOTS, Theme::GREY);
  text("now", PANEL_X, CHART_Y + CHART_H + 3, 1, Theme::GREY);
  rightAligned("+2h", PANEL_X + BAR_W * NOWCAST_SLOTS, CHART_Y + CHART_H + 3, 1, Theme::GREY);
}

/** @brief Crosshair at home (map centre). */
void homeMarker() {
  const int cx = RadarLoop::W / 2, cy = MAP_Y + RadarLoop::H / 2;
  canvas.drawCircle(cx, cy, 4, Theme::WHITE);
  canvas.drawFastHLine(cx - 8, cy, 5, Theme::WHITE);
  canvas.drawFastHLine(cx + 4, cy, 5, Theme::WHITE);
  canvas.drawFastVLine(cx, cy - 8, 5, Theme::WHITE);
  canvas.drawFastVLine(cx, cy + 4, 5, Theme::WHITE);
}

}  // namespace

void pageRadar(const UiModel &m) {
  const RadarLoop &r = m.radar;
  if (r.valid && r.count > 0) {
    const int idx = animationFrame(r);
    const RadarFrame &f = r.frames[idx];
    const bool newest = idx == r.count - 1;
    canvas.drawRGBBitmap(0, MAP_Y, f.pixels, RadarLoop::W, RadarLoop::H);
    homeMarker();

    // Frame time: white on the newest frame, grey while replaying.
    char t[6];
    localHHMM(f.time, t);
    canvas.fillRect(0, MAP_Y, 32, 10, Theme::BG);
    text(t, 1, MAP_Y + 1, 1, newest ? Theme::WHITE : Theme::LIGHT);
    text("(c)OpenStreetMap RainViewer", 1, MAP_Y + RadarLoop::H - 10, 1, Theme::GREY);

    // Progress bar along the bottom of the map.
    const int barY = MAP_Y + RadarLoop::H - 2;
    canvas.fillRect(0, barY, RadarLoop::W, 2, Theme::DKGREY);
    canvas.fillRect(0, barY, (idx + 1) * RadarLoop::W / r.count, 2, newest ? Theme::WHITE : Theme::BLUE);
  } else {
    canvas.drawRect(0, MAP_Y, RadarLoop::W, RadarLoop::H, Theme::DKGREY);
    centered(Net::connected() ? "Loading radar..." : "Connecting Wi-Fi...", RadarLoop::W / 2, MAP_Y + 50, 1, Theme::LIGHT);
  }
  nowcastPanel(m.weather);
}

}  // namespace UiDraw
