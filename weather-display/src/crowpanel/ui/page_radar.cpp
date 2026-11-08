/**
 * @file    page_radar.cpp
 * @brief   "Radar" page: large animated radar map plus the 2-hour rain nowcast.
 *
 * @details The map fills the left 520 x 380 px (about 375 x 275 km at zoom 7).
 *          Tap the map to pause/resume; while paused, tap the left or right
 *          third to step back or forward one frame.
 */
#include <utility>

#include "connectivity.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

constexpr int MAP_X = 0, MAP_Y = BODY_Y;
constexpr int PX = RadarLoop::W + M;      ///< Nowcast card x.
constexpr int PW = W - PX - M;            ///< Nowcast card width.

/** @brief Frame index to show (animation, or the paused frame). */
int frameIndex(const RadarLoop &r) {
  if (state.radarPaused && state.radarFrame >= 0) return min(state.radarFrame, r.count - 1);
  const uint32_t steps = r.count + RADAR_HOLD_STEPS;
  return min(static_cast<int>((millis() / RADAR_FRAME_MS) % steps), r.count - 1);
}

uint16_t rainColor(float mm) {
  if (mm < 0.25f) return rgb(150, 200, 255);
  if (mm < 1.0f) return C::BLUE;
  return C::YELLOW;
}

void map(const RadarLoop &r) {
  if (!r.valid || r.count == 0) {
    canvas.fillRect(MAP_X, MAP_Y, RadarLoop::W, RadarLoop::H, C::CARD);
    placeholder(MAP_X, MAP_Y, RadarLoop::W, RadarLoop::H, Net::connected() ? "Loading radar..." : "Connecting Wi-Fi...");
    return;
  }
  const int idx = frameIndex(r);
  const RadarFrame &f = r.frames[idx];
  canvas.pushImage(MAP_X, MAP_Y, RadarLoop::W, RadarLoop::H, reinterpret_cast<const lgfx::rgb565_t *>(f.pixels));

  // Home marker.
  const int cx = MAP_X + RadarLoop::W / 2, cy = MAP_Y + RadarLoop::H / 2;
  canvas.drawCircle(cx, cy, 7, C::TEXT);
  canvas.drawCircle(cx, cy, 8, C::BG);
  for (int s : {-1, 1}) {
    canvas.drawFastHLine(cx + s * 9 - (s < 0 ? 6 : 0), cy, 7, C::TEXT);
    canvas.drawFastVLine(cx, cy + s * 9 - (s < 0 ? 6 : 0), 7, C::TEXT);
  }

  // Frame time, newest highlighted.
  const bool newest = idx == r.count - 1;
  char t[6];
  hhmm(f.time, t);
  canvas.fillRoundRect(MAP_X + 8, MAP_Y + 8, 96, 34, 8, C::BG);
  text(t, MAP_X + 56, MAP_Y + 25, F::M, newest ? C::TEXT : C::MUTED, td::middle_center);
  if (state.radarPaused) {
    canvas.fillRoundRect(MAP_X + RadarLoop::W - 104, MAP_Y + 8, 96, 34, 8, C::BG);
    text("PAUSED", MAP_X + RadarLoop::W - 56, MAP_Y + 25, F::XS, C::YELLOW, td::middle_center);
  }
  text("(c) OpenStreetMap contributors, RainViewer", MAP_X + 6, MAP_Y + RadarLoop::H - 22, F::XS, C::MUTED);

  // Progress bar: one segment per frame.
  const int segW = RadarLoop::W / r.count;
  for (int i = 0; i < r.count; ++i) {
    canvas.fillRect(MAP_X + i * segW + 1, MAP_Y + RadarLoop::H - 5, segW - 2, 5,
                    i == idx ? (newest ? C::TEXT : C::ACCENT) : (i < idx ? rgb(40, 70, 110) : C::LINE));
  }
}

void nowcast(const WeatherData &wx) {
  constexpr int y = BODY_Y + M, h = BODY_H - 2 * M;
  card(PX, y, PW, h, "Rain, next 2 hours");
  if (!wx.valid || wx.nowcast.count == 0) return placeholder(PX, y, PW, h, "No nowcast yet");

  tm t;
  const RainSoon rs = Net::localTime(t) ? Weather::rainSoon(wx, t.tm_hour * 60 + t.tm_min) : RainSoon{};
  char head[16] = "--", detail[32] = "";
  uint16_t col = C::DIM;
  switch (rs.state) {
    case RainSoon::State::Dry:
      snprintf(head, sizeof(head), "Dry");
      snprintf(detail, sizeof(detail), "No rain expected");
      col = C::GREEN;
      break;
    case RainSoon::State::Starting:
      snprintf(head, sizeof(head), "%d min", rs.minutes);
      snprintf(detail, sizeof(detail), "until rain starts");
      col = C::BLUE;
      break;
    case RainSoon::State::Raining:
      snprintf(head, sizeof(head), "Raining");
      if (rs.minutes >= 0) snprintf(detail, sizeof(detail), "easing in ~%d min", rs.minutes);
      else snprintf(detail, sizeof(detail), "for at least 2 hours");
      col = C::BLUE;
      break;
    default: break;
  }
  text(head, PX + 16, y + 40, F::L, col);
  text(detail, PX + 16, y + 92, F::S, C::MUTED);

  // Bar chart: sqrt scale so drizzle shows; 2 mm per 15 min fills.
  const int cx0 = PX + 16, cw = PW - 32, cTop = y + 130, cBot = y + 240;
  const int bw = cw / NOWCAST_SLOTS;
  float total = 0;
  for (int i = 0; i < wx.nowcast.count; ++i) {
    const float mm = wx.nowcast.mm[i];
    total += mm;
    if (mm < 0.01f) continue;
    const int bh = constrain(static_cast<int>(sqrtf(mm / 2.0f) * (cBot - cTop)), 3, cBot - cTop);
    canvas.fillRect(cx0 + i * bw + 2, cBot - bh, bw - 4, bh, rainColor(mm));
  }
  canvas.drawFastHLine(cx0, cBot, cw, C::LINE);
  text("now", cx0, cBot + 6, F::XS, C::MUTED);
  text("+1 h", cx0 + cw / 2, cBot + 6, F::XS, C::MUTED, td::top_center);
  text("+2 h", cx0 + cw, cBot + 6, F::XS, C::MUTED, td::top_right);
  textf(PX + 16, cBot + 26, F::XS, C::MUTED, td::top_left, "Total %.1f mm forecast", total);

  // Intensity key.
  int kx = PX + 16;
  const int ky = y + h - 34;
  for (auto &k : {std::make_pair(rgb(150, 200, 255), "light"), std::make_pair(C::BLUE, "moderate"),
                  std::make_pair(C::YELLOW, "heavy")}) {
    canvas.fillRect(kx, ky + 4, 12, 12, k.first);
    kx += 16 + text(k.second, kx + 16, ky + 2, F::XS, C::MUTED) + 12;
  }
  text("Tap map: pause / step", PX + 16, y + h - 60, F::XS, C::DIM);
}

}  // namespace

void pageRadar() {
  map(DataService::radar());
  nowcast(DataService::weather());
}

bool tapRadar(int x, int y) {
  if (x >= RadarLoop::W || y < MAP_Y || y >= MAP_Y + RadarLoop::H) return false;
  DataService::Guard g;
  const RadarLoop &r = DataService::radar();
  if (r.count == 0) return false;
  if (!state.radarPaused) {
    state.radarPaused = true;
    state.radarFrame = frameIndex(r);
    return true;
  }
  if (x < RadarLoop::W / 3) state.radarFrame = max(0, state.radarFrame - 1);
  else if (x > RadarLoop::W * 2 / 3) state.radarFrame = min<int>(r.count - 1, state.radarFrame + 1);
  else state.radarPaused = false;  // Middle third resumes.
  return true;
}

}  // namespace UiDraw
