/**
 * @file    ui_draw.cpp
 * @brief   Drawing helpers shared by the CrowPanel pages.
 */
#include "ui_draw.h"

#include <stdarg.h>

namespace UiDraw {

LGFX_Sprite canvas;
UiState state;

namespace {

/** @brief Font table indexed by F. */
const lgfx::IFont *fontFor(F f) {
  switch (f) {
    case F::XS: return &fonts::DejaVu12;
    case F::S: return &fonts::DejaVu18;
    case F::M: return &fonts::DejaVu24;
    case F::L: return &fonts::DejaVu40;
    case F::XL: return &fonts::DejaVu56;
    case F::XXL: return &fonts::DejaVu72;
    case F::HUGE: return &fonts::DejaVu72;
  }
  return &fonts::DejaVu18;
}

/** @brief Text scale for F::HUGE (72 px font drawn 1.5x). */
float scaleFor(F f) { return f == F::HUGE ? 1.5f : 1.0f; }

}  // namespace

void setFont(F f) {
  canvas.setFont(fontFor(f));
  canvas.setTextSize(scaleFor(f));
}

int fontHeight(F f) {
  setFont(f);
  return canvas.fontHeight();
}

int textWidth(const char *s, F f) {
  setFont(f);
  return canvas.textWidth(s);
}

int text(const char *s, int x, int y, F f, uint16_t color, Datum d) {
  setFont(f);
  canvas.setTextColor(color);
  canvas.setTextDatum(d);
  return canvas.drawString(s, x, y);
}

int textf(int x, int y, F f, uint16_t color, Datum d, const char *fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  return text(buf, x, y, f, color, d);
}

int temp(float t, int x, int y, F f, uint16_t color, Datum d, int decimals) {
  char buf[16];
  if (isnan(t)) snprintf(buf, sizeof(buf), "--");
  else snprintf(buf, sizeof(buf), "%.*f", decimals, t);
  const int numW = textWidth(buf, f);
  const int fh = fontHeight(f);
  const int r = max(2, fh / 10);
  const int gap = 1 - fh / 18;                   // The glyph advance already leaves space; pull the ring in.
  const int total = numW + gap + r * 2 + 1;
  int left = x;
  const int h = d & 3;  // 0 left, 1 centre, 2 right.
  if (h == 1) left = x - total / 2;
  else if (h == 2) left = x - total;
  text(buf, left, y, f, color, td::top_left);
  const int cx = left + numW + gap + r, cy = y + fh / 5 + r;
  const int thick = max(1, r / 4);               // Ring stroke grows with the font.
  for (int i = 0; i < thick; ++i) canvas.drawCircle(cx, cy, r - i, color);
  return total;
}

void card(int x, int y, int w, int h, const char *title) {
  canvas.fillRoundRect(x, y, w, h, 10, C::CARD);
  if (title) text(title, x + 14, y + 10, F::S, C::MUTED);
}

int chip(int x, int y, const char *label, uint16_t bg, uint16_t fg, F f) {
  const int tw = textWidth(label, f), th = fontHeight(f);
  const int w = tw + 20, h = th + 10;
  canvas.fillRoundRect(x, y, w, h, h / 2, bg);
  text(label, x + 10, y + 5, f, fg);
  return w;
}

void placeholder(int x, int y, int w, int h, const char *msg) {
  text(msg, x + w / 2, y + h / 2, F::S, C::DIM, td::middle_center);
}

void wifiBars(int x, int y, int bars, bool connected) {
  if (!connected) {
    canvas.drawWideLine(x + 2, y + 2, x + 20, y + 18, 1.5f, C::RED);
    canvas.drawWideLine(x + 20, y + 2, x + 2, y + 18, 1.5f, C::RED);
    return;
  }
  for (int i = 0; i < 4; ++i) {
    const int h = 5 + i * 4;
    canvas.fillRect(x + i * 6, y + 18 - h, 4, h, i < bars ? C::TEXT : C::LINE);
  }
}

void warningTriangle(int x, int y, int s, uint16_t color) {
  canvas.fillTriangle(x + s / 2, y, x, y + s - 1, x + s - 1, y + s - 1, color);
  const int bw = max(2, s / 9);
  canvas.fillRect(x + s / 2 - bw / 2, y + s * 3 / 10, bw, s * 4 / 10, C::BG);
  canvas.fillRect(x + s / 2 - bw / 2, y + s * 8 / 10, bw, bw, C::BG);
}

uint16_t warningColor(WarningLevel l) {
  switch (l) {
    case WarningLevel::Red: return C::RED;
    case WarningLevel::Amber: return C::ORANGE;
    default: return C::YELLOW;
  }
}

uint16_t auroraColor(AuroraLevel l) {
  switch (l) {
    case AuroraLevel::Red: return C::RED;
    case AuroraLevel::Amber: return C::ORANGE;
    case AuroraLevel::Yellow: return C::YELLOW;
    case AuroraLevel::Green: return C::GREEN;
    default: return C::DIM;
  }
}

uint16_t aqiColor(uint8_t b) {
  static const uint16_t colors[] = {rgb(80, 220, 200), C::GREEN, C::YELLOW, C::ORANGE, C::RED, C::PURPLE};
  return colors[min<uint8_t>(b, 5)];
}

uint16_t carbonColor(CarbonIndex i) {
  switch (i) {
    case CarbonIndex::VeryLow: return rgb(40, 170, 90);
    case CarbonIndex::Low: return C::GREEN;
    case CarbonIndex::Moderate: return C::YELLOW;
    case CarbonIndex::High: return C::ORANGE;
    case CarbonIndex::VeryHigh: return C::RED;
    default: return C::DIM;
  }
}

uint16_t frostColor(FrostLevel l) {
  switch (l) {
    case FrostLevel::Low: return rgb(150, 200, 255);
    case FrostLevel::Moderate: return C::CYAN;
    case FrostLevel::High: return rgb(200, 160, 255);
    default: return C::DIM;
  }
}

void hhmm(time_t t, char *buf) {
  tm lt;
  localtime_r(&t, &lt);
  snprintf(buf, 6, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

void ago(uint32_t atMs, char *buf, size_t n) {
  if (atMs == 0) {
    snprintf(buf, n, "never");
    return;
  }
  const uint32_t s = (millis() - atMs) / 1000;
  if (s < 90) snprintf(buf, n, "%lu s ago", static_cast<unsigned long>(s));
  else if (s < 5400) snprintf(buf, n, "%lu min ago", static_cast<unsigned long>(s / 60));
  else snprintf(buf, n, "%lu h ago", static_cast<unsigned long>(s / 3600));
}

}  // namespace UiDraw
