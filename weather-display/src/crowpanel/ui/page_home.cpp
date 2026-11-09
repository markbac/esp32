/**
 * @file    page_home.cpp
 * @brief   "Home" page: Home Assistant summary.
 *
 * @details Temperatures, humidity, people, heating, biggest power users,
 *          lights on and open doors/windows, all from one template-API call.
 *          Without a token the page explains how to add one.
 */
#include "config.h"
#include "connectivity.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

/** @brief Name (truncated) on the left, value on the right. */
void readingRow(int x, int y, int w, const HaReading &r, uint16_t col, int decimals) {
  textf(x, y, F::XS, C::MUTED, td::top_left, "%.20s", r.name);
  textf(x + w, y - 3, F::S, col, td::top_right, "%.*f%s", decimals, r.value, r.unit[0] == '%' ? "%" : "");
}

void setupHelp(int lastStatus) {
  constexpr int x = M, y = BODY_Y + M, w = W - 2 * M, h = BODY_H - 2 * M;
  card(x, y, w, h, "Home Assistant");
  int ty = y + 50;
  if (lastStatus == 401) {
    text("Home Assistant rejected the token (401).", x + 24, ty, F::M, C::ORANGE);
    ty += 44;
  } else {
    text("Connect Home Assistant", x + 24, ty, F::M, C::TEXT);
    ty += 44;
  }
  const char *steps[] = {"1. In Home Assistant open your profile (bottom-left) > Security.",
                         "2. Under Long-lived access tokens choose Create token, name it 'CrowPanel'.",
                         "3. On a phone or PC open the settings page:",
                         "      http://" HOSTNAME ".local/settings",
                         "4. Paste the token under Home Assistant and Save (no re-flash needed)."};
  for (const char *s : steps) {
    text(s, x + 24, ty, F::S, s[0] == ' ' ? C::ACCENT : C::MUTED);
    ty += 34;
  }
}

void tempsCard(const HaData &d) {
  constexpr int x = M, y = BODY_Y + M, w = 392, h = 236;
  card(x, y, w, h, "Temperatures");
  if (!d.tempCount) return placeholder(x, y, w, h, "No temperature sensors");
  const int colW = (w - 40) / 2;
  for (uint8_t i = 0; i < d.tempCount && i < 16; ++i) {
    const int cx = x + 16 + (i / 8) * (colW + 8), cy = y + 40 + (i % 8) * 24;
    const float v = d.temps[i].value;
    readingRow(cx, cy, colW, d.temps[i], v < 12 ? C::CYAN : v > 24 ? C::ORANGE : C::TEXT, 1);
  }
}

void humidityCard(const HaData &d) {
  constexpr int x = M, y = BODY_Y + 252, w = 392, h = 120;
  card(x, y, w, h, "Humidity");
  if (!d.humidityCount) return placeholder(x, y, w, h, "No humidity sensors");
  const int colW = (w - 40) / 2;
  for (uint8_t i = 0; i < d.humidityCount && i < 6; ++i) {
    const int cx = x + 16 + (i / 3) * (colW + 8), cy = y + 38 + (i % 3) * 24;
    readingRow(cx, cy, colW, d.humidity[i], d.humidity[i].value > 70 ? C::BLUE : C::TEXT, 0);
  }
}

void peopleCard(const HaData &d) {
  constexpr int x = 400, y = BODY_Y + M, w = 392, h = 104;
  card(x, y, w, h, "People & house");
  int cx = x + 16;
  for (uint8_t i = 0; i < d.peopleCount; ++i) {
    const bool home = !strcmp(d.people[i].state, "home");
    char label[40];
    snprintf(label, sizeof(label), "%.12s: %.10s", d.people[i].name, home ? "home" : (!strcmp(d.people[i].state, "not_home") ? "away" : d.people[i].state));
    const int cw = textWidth(label, F::XS) + 20;
    if (cx + cw > x + w - 12) break;
    chip(cx, y + 36, label, home ? rgb(28, 80, 48) : C::CARD2, home ? C::GREEN : C::MUTED, F::XS);
    cx += cw + 6;
  }
  textf(x + 16, y + 74, F::XS, C::TEXT, td::top_left, "Lights on %u / %u", d.lightsOn, d.lightsTotal);
  if (d.openCount) {
    textf(x + 150, y + 74, F::XS, C::ORANGE, td::top_left, "Open: %.22s%s", d.open[0], d.openCount > 1 ? " +more" : "");
  } else {
    text("Doors & windows closed", x + 150, y + 74, F::XS, C::GREEN);
  }
}

void climateCard(const HaData &d) {
  constexpr int x = 400, y = BODY_Y + 120, w = 392, h = 124;
  card(x, y, w, h, "Heating");
  if (!d.climateCount) return placeholder(x, y, w, h, "No climate entities");
  const int cw = (w - 32) / d.climateCount;
  for (uint8_t i = 0; i < d.climateCount; ++i) {
    const HaClimate &c = d.climate[i];
    const int cx = x + 16 + i * cw;
    textf(cx, y + 36, F::XS, C::MUTED, td::top_left, "%.16s", c.name);
    temp(c.current, cx, y + 54, F::M, C::TEXT, td::top_left, 1);
    if (!isnan(c.target)) textf(cx, y + 88, F::XS, C::MUTED, td::top_left, "target %.1f", c.target);
    const bool heating = !strcmp(c.action, "heating");
    text(c.action, cx, y + 104, F::XS, heating ? C::ORANGE : C::DIM);
  }
}

void powerCard(const HaData &d) {
  constexpr int x = 400, y = BODY_Y + 252, w = 392, h = 120;
  card(x, y, w, h, "Power");
  if (!d.powerCount) return placeholder(x, y, w, h, "No power sensors");
  const float top = max(1.0f, d.power[0].value);
  for (uint8_t i = 0; i < d.powerCount && i < 4; ++i) {
    const int ry = y + 36 + i * 20;
    textf(x + 16, ry, F::XS, C::MUTED, td::top_left, "%.18s", d.power[i].name);
    const int bx = x + 170, bw = w - 170 - 80;
    canvas.fillRoundRect(bx, ry + 3, max(4, static_cast<int>(d.power[i].value / top * bw)), 10, 5, C::YELLOW);
    textf(x + w - 16, ry, F::XS, C::TEXT, td::top_right, "%.0f %s", d.power[i].value, d.power[i].unit);
  }
}

}  // namespace

void pageHome() {
  const HaData &d = DataService::homeAssistant();
  if (!d.configured || (!d.valid && d.lastStatus == 401)) return setupHelp(d.lastStatus);
  if (!d.valid) {
    placeholder(0, BODY_Y, W, BODY_H, Net::connected() ? "Contacting Home Assistant..." : "Connecting Wi-Fi...");
    return;
  }
  tempsCard(d);
  humidityCard(d);
  peopleCard(d);
  climateCard(d);
  powerCard(d);
}

}  // namespace UiDraw
