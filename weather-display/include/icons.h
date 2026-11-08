/**
 * @file    icons.h
 * @brief   Scalable vector icons (weather, sun events, moon phase, aircraft).
 *
 * @details Header-only templates so the same artwork draws on any graphics
 *          library with the usual primitives (fillCircle, fillTriangle,
 *          drawLine, fillRoundRect, drawFastHLine, fillRect, drawCircle):
 *          Adafruit_GFX on the Feather and LovyanGFX on the CrowPanel.
 *
 *          Weather icons are designed on a 48 x 48 grid centred on (0, 0) and
 *          scaled at draw time; lines thicken with size so large icons on the
 *          7" panel keep their weight.
 */
#pragma once

#include <Arduino.h>

#include "theme.h"

/** @brief Visual icon categories that WMO codes are grouped into. */
enum class WeatherIcon : uint8_t {
  Clear,         ///< Sun (day) or moon (night).
  PartlyCloudy,  ///< Sun/moon behind a cloud.
  Cloudy,        ///< Overcast.
  Fog,           ///< Horizontal haze bars.
  Drizzle,       ///< Cloud with light dots.
  Rain,          ///< Cloud with rain streaks.
  Showers,       ///< Sun/moon, cloud and rain.
  Sleet,         ///< Freezing rain/drizzle: rain and snow mixed.
  Snow,          ///< Cloud with snowflakes.
  Thunder,       ///< Dark cloud with lightning bolt.
};

namespace Icons {

/** @brief Map a WMO weather code to an icon category. */
inline WeatherIcon fromWmo(int c) {
  if (c == 0) return WeatherIcon::Clear;
  if (c == 1 || c == 2) return WeatherIcon::PartlyCloudy;
  if (c == 3) return WeatherIcon::Cloudy;
  if (c == 45 || c == 48) return WeatherIcon::Fog;
  if (c >= 51 && c <= 55) return WeatherIcon::Drizzle;
  if (c == 56 || c == 57 || c == 66 || c == 67) return WeatherIcon::Sleet;
  if (c >= 61 && c <= 65) return WeatherIcon::Rain;
  if (c >= 80 && c <= 82) return WeatherIcon::Showers;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WeatherIcon::Snow;
  if (c >= 95) return WeatherIcon::Thunder;
  return WeatherIcon::Cloudy;
}

namespace detail {

/** @brief Maps 48x48 design-grid coordinates onto screen pixels. */
template <class G>
struct Pen {
  G &g;          ///< Drawing surface.
  int cx;        ///< Screen x of grid origin.
  int cy;        ///< Screen y of grid origin.
  float s;       ///< Scale factor (pixels per grid unit).
  uint16_t bg;   ///< Background colour (used to mask the crescent moon).

  int x(float v) const { return cx + lroundf(v * s); }
  int y(float v) const { return cy + lroundf(v * s); }
  int len(float v) const { return max(1L, lroundf(v * s)); }

  /** @brief Line whose thickness grows with scale (1 px at 48 px icons, ~3 px at 140 px). */
  void line(float x0, float y0, float x1, float y1, uint16_t c) const {
    const int t = max(1, static_cast<int>(s * 1.1f));
    for (int i = 0; i < t; ++i) g.drawLine(x(x0) + i, y(y0), x(x1) + i, y(y1), c);
  }
};

template <class G>
void sun(const Pen<G> &p, float x, float y, float r) {
  for (int i = 0; i < 8; ++i) {
    const float a = i * PI / 4;
    p.line(x + cosf(a) * (r + 3), y + sinf(a) * (r + 3), x + cosf(a) * (r + 8), y + sinf(a) * (r + 8), Theme::YELLOW);
  }
  p.g.fillCircle(p.x(x), p.y(y), p.len(r), Theme::YELLOW);
}

template <class G>
void crescent(const Pen<G> &p, float x, float y, float r) {
  p.g.fillCircle(p.x(x), p.y(y), p.len(r), Theme::LIGHT);
  p.g.fillCircle(p.x(x + r * 0.5f), p.y(y - r * 0.35f), p.len(r * 0.85f), p.bg);
}

template <class G>
void sky(const Pen<G> &p, float x, float y, float r, bool day) {
  if (day) sun(p, x, y, r);
  else crescent(p, x, y, r);
}

template <class G>
void cloud(const Pen<G> &p, float x, float y, uint16_t c) {
  p.g.fillCircle(p.x(x - 10), p.y(y + 2), p.len(9), c);
  p.g.fillCircle(p.x(x + 2), p.y(y - 4), p.len(12), c);
  p.g.fillCircle(p.x(x + 13), p.y(y + 3), p.len(8), c);
  p.g.fillRoundRect(p.x(x - 19), p.y(y + 2), p.len(40), p.len(10), p.len(5), c);
}

template <class G>
void rain(const Pen<G> &p) {
  for (int i = 0; i < 4; ++i) p.line(-12 + i * 8, 11, -15 + i * 8, 19, Theme::BLUE);
}

template <class G>
void drizzle(const Pen<G> &p) {
  for (int i = 0; i < 4; ++i) p.g.fillCircle(p.x(-12 + i * 8), p.y(14 + (i % 2) * 4), p.len(1.5f), Theme::BLUE);
}

template <class G>
void snow(const Pen<G> &p) {
  for (int i = 0; i < 4; ++i) p.g.fillCircle(p.x(-12 + i * 8), p.y(13 + (i % 2) * 5), p.len(2.5f), Theme::WHITE);
}

template <class G>
void bolt(const Pen<G> &p) {
  p.g.fillTriangle(p.x(3), p.y(4), p.x(-6), p.y(15), p.x(1), p.y(15), Theme::YELLOW);
  p.g.fillTriangle(p.x(-2), p.y(13), p.x(6), p.y(13), p.x(-4), p.y(24), Theme::YELLOW);
}

}  // namespace detail

/**
 * @brief Draw a weather icon.
 * @param gfx      Target surface (screen, canvas or sprite).
 * @param cx, cy   Icon centre, pixels.
 * @param size     Bounding-box edge length, pixels (48 = native).
 * @param wmoCode  WMO weather code.
 * @param isDay    Selects sun (true) or moon (false) artwork.
 * @param bg       Background colour behind the icon (for the moon's mask).
 */
template <class G>
void draw(G &gfx, int cx, int cy, int size, int wmoCode, bool isDay, uint16_t bg = Theme::BG) {
  using namespace detail;
  const Pen<G> p{gfx, cx, cy, size / 48.0f, bg};

  switch (fromWmo(wmoCode)) {
    case WeatherIcon::Clear:
      sky(p, 0, 0, 11, isDay);
      break;
    case WeatherIcon::PartlyCloudy:
      sky(p, -8, -8, 9, isDay);
      cloud(p, 4, 5, Theme::LIGHT);
      break;
    case WeatherIcon::Cloudy:
      cloud(p, -6, -4, Theme::GREY);
      cloud(p, 3, 4, Theme::LIGHT);
      break;
    case WeatherIcon::Fog:
      for (int i = 0; i < 4; ++i)
        gfx.fillRoundRect(p.x(-18 + (i % 2) * 4), p.y(-13 + i * 8), p.len(32), p.len(4), p.len(2), Theme::LIGHT);
      break;
    case WeatherIcon::Drizzle:
      cloud(p, 0, -6, Theme::LIGHT);
      drizzle(p);
      break;
    case WeatherIcon::Rain:
      cloud(p, 0, -6, Theme::GREY);
      rain(p);
      break;
    case WeatherIcon::Showers:
      sky(p, -10, -12, 8, isDay);
      cloud(p, 2, -4, Theme::LIGHT);
      rain(p);
      break;
    case WeatherIcon::Sleet:
      cloud(p, 0, -6, Theme::GREY);
      p.line(-12, 11, -15, 19, Theme::BLUE);
      gfx.fillCircle(p.x(-4), p.y(15), p.len(2.5f), Theme::WHITE);
      p.line(4, 11, 1, 19, Theme::BLUE);
      gfx.fillCircle(p.x(12), p.y(15), p.len(2.5f), Theme::WHITE);
      break;
    case WeatherIcon::Snow:
      cloud(p, 0, -6, Theme::LIGHT);
      snow(p);
      break;
    case WeatherIcon::Thunder:
      cloud(p, 0, -8, Theme::GREY);
      bolt(p);
      break;
  }
}

/**
 * @brief Half sun on a horizon with an up (sunrise) or down (sunset) arrow.
 * @param cx      Centre x.
 * @param cy      Horizon y.
 * @param size    Overall width, pixels.
 * @param rising  true = sunrise (arrow up), false = sunset (arrow down).
 * @param bg      Background colour (hides the lower half of the sun).
 */
template <class G>
void sunEvent(G &gfx, int cx, int cy, int size, bool rising, uint16_t bg = Theme::BG) {
  const int r = size / 4;
  const int ray = max(3, size / 8);
  for (int i = 1; i < 8; ++i) {
    const float a = PI + i * PI / 8;  // Upper semicircle.
    gfx.drawLine(cx + cosf(a) * (r + 2), cy + sinf(a) * (r + 2), cx + cosf(a) * (r + 2 + ray), cy + sinf(a) * (r + 2 + ray),
                 Theme::YELLOW);
  }
  gfx.fillCircle(cx, cy, r, Theme::ORANGE);
  gfx.fillRect(cx - r - 1, cy + 1, 2 * r + 3, r + 1, bg);  // Hide lower half.
  gfx.drawFastHLine(cx - size / 2, cy + 1, size, Theme::LIGHT);
  const int a = max(4, size / 6), ay = cy + 4;
  if (rising) gfx.fillTriangle(cx, ay, cx - a, ay + a, cx + a, ay + a, Theme::YELLOW);
  else gfx.fillTriangle(cx, ay + a, cx - a, ay, cx + a, ay, Theme::ORANGE);
}

/**
 * @brief Moon disc shaded for the given phase (northern-hemisphere view).
 * @param phase  0 = new, 0.25 = first quarter (right half lit), 0.5 = full, 0.75 = last quarter.
 */
template <class G>
void moon(G &gfx, int cx, int cy, int r, float phase) {
  constexpr uint16_t LIT = 0xF79E;   // Warm white.
  constexpr uint16_t DARK = 0x2945;  // Earthshine grey.
  gfx.fillCircle(cx, cy, r, DARK);
  const float k = cosf(2.0f * PI * phase);  // Terminator position factor.
  const bool waxing = phase < 0.5f;
  for (int dy = -r; dy <= r; ++dy) {
    const float w = sqrtf(static_cast<float>(r * r - dy * dy));
    // Waxing: lit from x = w*k to w (right side). Waning: lit from -w to -w*k (left side).
    const float x0 = waxing ? w * k : -w;
    const float x1 = waxing ? w : -w * k;
    if (x1 > x0) gfx.drawFastHLine(cx + lroundf(x0), cy + dy, max(1L, lroundf(x1 - x0)), LIT);
  }
  gfx.drawCircle(cx, cy, r, Theme::GREY);
}

/**
 * @brief Aircraft silhouette rotated to a heading (an arrowhead below 12 px).
 * @param heading  Degrees true; 0 points up (north).
 */
template <class G>
void plane(G &gfx, int cx, int cy, int size, float heading, uint16_t color) {
  const float a = heading * DEG_TO_RAD, c = cosf(a), s = sinf(a);
  const float k = size / 24.0f;  // Design grid: wingspan 24 units, nose at y = -12.
  auto px = [&](float x, float y) { return static_cast<int>(cx + lroundf((x * c - y * s) * k)); };
  auto py = [&](float x, float y) { return static_cast<int>(cy + lroundf((x * s + y * c) * k)); };
  auto tri = [&](float x0, float y0, float x1, float y1, float x2, float y2) {
    gfx.fillTriangle(px(x0, y0), py(x0, y0), px(x1, y1), py(x1, y1), px(x2, y2), py(x2, y2), color);
  };

  if (size < 12) {
    tri(0, -12, -8, 10, 8, 10);
    return;
  }
  tri(0, -12, -2, -8, 2, -8);   // Nose.
  tri(-2, -8, 2, -8, -2, 10);   // Fuselage (rectangle as two triangles).
  tri(2, -8, 2, 10, -2, 10);
  tri(0, -4, 12, 3, 0, 2);      // Right wing.
  tri(12, 3, 12, 5, 0, 2);
  tri(0, -4, -12, 3, 0, 2);     // Left wing.
  tri(-12, 3, -12, 5, 0, 2);
  tri(0, 6, 5, 11, -5, 11);     // Tailplane.
}

}  // namespace Icons
