/**
 * @file    icons.cpp
 * @brief   Implementation of the scalable weather icon set.
 */
#include "icons.h"

#include "theme.h"

namespace {

/**
 * @brief Maps 48x48 design-grid coordinates onto screen pixels.
 */
struct Pen {
  Adafruit_GFX &g;  ///< Drawing surface.
  int cx;           ///< Screen x of grid origin.
  int cy;           ///< Screen y of grid origin.
  float s;          ///< Scale factor (pixels per grid unit).

  /** @brief Grid x -> screen x. */
  int x(float v) const { return cx + lroundf(v * s); }
  /** @brief Grid y -> screen y. */
  int y(float v) const { return cy + lroundf(v * s); }
  /** @brief Grid length -> pixels (at least 1). */
  int len(float v) const { return max(1L, lroundf(v * s)); }

  /** @brief Line, thickened to 2 px when the icon is drawn large. */
  void line(float x0, float y0, float x1, float y1, uint16_t c) const {
    g.drawLine(x(x0), y(y0), x(x1), y(y1), c);
    if (s >= 0.9f) g.drawLine(x(x0) + 1, y(y0), x(x1) + 1, y(y1), c);
  }
};

/** @brief Sun disc with eight rays. */
void sun(const Pen &p, float x, float y, float r) {
  for (int i = 0; i < 8; ++i) {
    const float a = i * PI / 4;
    p.line(x + cosf(a) * (r + 3), y + sinf(a) * (r + 3), x + cosf(a) * (r + 8), y + sinf(a) * (r + 8), Theme::YELLOW);
  }
  p.g.fillCircle(p.x(x), p.y(y), p.len(r), Theme::YELLOW);
}

/** @brief Crescent moon made by masking one disc with a background disc. */
void moon(const Pen &p, float x, float y, float r) {
  p.g.fillCircle(p.x(x), p.y(y), p.len(r), Theme::LIGHT);
  p.g.fillCircle(p.x(x + r * 0.5f), p.y(y - r * 0.35f), p.len(r * 0.85f), Theme::BG);
}

/** @brief Sun by day, moon by night. */
void sky(const Pen &p, float x, float y, float r, bool day) { day ? sun(p, x, y, r) : moon(p, x, y, r); }

/** @brief Three-lobed cloud with a flat base, centred near (x, y). */
void cloud(const Pen &p, float x, float y, uint16_t c) {
  p.g.fillCircle(p.x(x - 10), p.y(y + 2), p.len(9), c);
  p.g.fillCircle(p.x(x + 2), p.y(y - 4), p.len(12), c);
  p.g.fillCircle(p.x(x + 13), p.y(y + 3), p.len(8), c);
  p.g.fillRoundRect(p.x(x - 19), p.y(y + 2), p.len(40), p.len(10), p.len(5), c);
}

/** @brief Four slanted rain streaks under a cloud. */
void rain(const Pen &p) {
  for (int i = 0; i < 4; ++i) p.line(-12 + i * 8, 11, -15 + i * 8, 19, Theme::BLUE);
}

/** @brief Four short drizzle dots under a cloud. */
void drizzle(const Pen &p) {
  for (int i = 0; i < 4; ++i) p.g.fillCircle(p.x(-12 + i * 8), p.y(14 + (i % 2) * 4), p.len(1.5f), Theme::BLUE);
}

/** @brief Four snowflake dots under a cloud. */
void snow(const Pen &p) {
  for (int i = 0; i < 4; ++i) p.g.fillCircle(p.x(-12 + i * 8), p.y(13 + (i % 2) * 5), p.len(2.5f), Theme::WHITE);
}

/** @brief Two-triangle lightning bolt. */
void bolt(const Pen &p) {
  p.g.fillTriangle(p.x(3), p.y(4), p.x(-6), p.y(15), p.x(1), p.y(15), Theme::YELLOW);
  p.g.fillTriangle(p.x(-2), p.y(13), p.x(6), p.y(13), p.x(-4), p.y(24), Theme::YELLOW);
}

}  // namespace

namespace Icons {

WeatherIcon fromWmo(int c) {
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

void draw(Adafruit_GFX &gfx, int cx, int cy, int size, int wmoCode, bool isDay) {
  const Pen p{gfx, cx, cy, size / 48.0f};

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
      for (int i = 0; i < 4; ++i) gfx.fillRoundRect(p.x(-18 + (i % 2) * 4), p.y(-13 + i * 8), p.len(32), p.len(4), p.len(2), Theme::LIGHT);
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

void sunEvent(Adafruit_GFX &gfx, int cx, int cy, int size, bool rising) {
  const int r = size / 4;
  // Rays above the horizon only.
  for (int i = 1; i < 8; ++i) {
    const float a = PI + i * PI / 8;  // Upper semicircle.
    gfx.drawLine(cx + cosf(a) * (r + 2), cy + sinf(a) * (r + 2), cx + cosf(a) * (r + 5), cy + sinf(a) * (r + 5), Theme::YELLOW);
  }
  gfx.fillCircle(cx, cy, r, Theme::ORANGE);
  gfx.fillRect(cx - r - 1, cy + 1, 2 * r + 3, r + 1, Theme::BG);  // Hide lower half.
  gfx.drawFastHLine(cx - size / 2, cy + 1, size, Theme::LIGHT);
  // Arrow below the horizon.
  const int ay = cy + 4;
  if (rising) gfx.fillTriangle(cx, ay, cx - 4, ay + 5, cx + 4, ay + 5, Theme::YELLOW);
  else gfx.fillTriangle(cx, ay + 5, cx - 4, ay, cx + 4, ay, Theme::ORANGE);
}

void moon(Adafruit_GFX &gfx, int cx, int cy, int r, float phase) {
  constexpr uint16_t LIT = 0xF79E;    // Warm white.
  constexpr uint16_t DARK = 0x2945;   // Earthshine grey.
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

void plane(Adafruit_GFX &gfx, int cx, int cy, int size, float heading, uint16_t color) {
  const float a = heading * DEG_TO_RAD, c = cosf(a), s = sinf(a);
  const float k = size / 24.0f;  // Design grid: wingspan 24 units, nose at y = -12.
  auto px = [&](float x, float y) { return cx + lroundf((x * c - y * s) * k); };
  auto py = [&](float x, float y) { return cy + lroundf((x * s + y * c) * k); };
  auto tri = [&](float x0, float y0, float x1, float y1, float x2, float y2) {
    gfx.fillTriangle(px(x0, y0), py(x0, y0), px(x1, y1), py(x1, y1), px(x2, y2), py(x2, y2), color);
  };

  if (size < 12) {  // Small: a simple arrowhead.
    tri(0, -12, -8, 10, 8, 10);
    return;
  }
  tri(0, -12, -2, -8, 2, -8);     // Nose.
  tri(-2, -8, 2, -8, -2, 10);      // Fuselage (rectangle as two triangles).
  tri(2, -8, 2, 10, -2, 10);
  tri(0, -4, 12, 3, 0, 2);         // Right wing.
  tri(12, 3, 12, 5, 0, 2);
  tri(0, -4, -12, 3, 0, 2);        // Left wing.
  tri(-12, 3, -12, 5, 0, 2);
  tri(0, 6, 5, 11, -5, 11);        // Tailplane.
}

}  // namespace Icons
