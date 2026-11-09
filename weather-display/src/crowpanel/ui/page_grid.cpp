/**
 * @file    page_grid.cpp
 * @brief   "Grid" page: regional electricity carbon intensity, generation mix and 48 h forecast.
 *
 * @details Highlights the greenest 3-hour window in the next 24 hours, which is
 *          the best time to run washing machines, dishwashers or EV charging.
 */
#include <algorithm>

#include "connectivity.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

/** @brief Colour for each fuel in the mix bars. */
uint16_t fuelColor(Fuel f) {
  switch (f) {
    case Fuel::Wind: return rgb(120, 200, 255);
    case Fuel::Solar: return C::YELLOW;
    case Fuel::Nuclear: return C::PURPLE;
    case Fuel::Hydro: return C::BLUE;
    case Fuel::Biomass: return rgb(140, 190, 90);
    case Fuel::Imports: return C::MUTED;
    case Fuel::Gas: return C::ORANGE;
    case Fuel::Coal: return rgb(120, 90, 70);
    default: return C::DIM;
  }
}

void nowCard(const CarbonData &c) {
  constexpr int x = M, y = BODY_Y + M, w = 384, h = 236;
  card(x, y, w, h, "Carbon intensity now");
  const uint16_t col = carbonColor(c.nowIndex);
  const int nw = textf(x + 20, y + 40, F::XXL, col, td::top_left, "%u", c.nowGPerKwh);
  text("g CO2/kWh", x + 30 + nw, y + 72, F::S, C::MUTED);
  text(Carbon::indexName(c.nowIndex), x + 20, y + 124, F::M, col);

  // Low-carbon share (wind, solar, nuclear, hydro, biomass).
  float green = 0;
  for (Fuel f : {Fuel::Wind, Fuel::Solar, Fuel::Nuclear, Fuel::Hydro, Fuel::Biomass}) green += c.mix[static_cast<int>(f)];
  textf(x + 20, y + 160, F::S, C::TEXT, td::top_left, "%.0f%% low-carbon", green);

  const int best = c.greenestWindow(6, 48);  // 3 h window within 24 h.
  if (best >= 0) {
    char a[6], b[6];
    hhmm(c.forecast[best].from, a);
    hhmm(c.forecast[best].from + 3 * 3600, b);
    uint32_t sum = 0;
    for (int k = 0; k < 6; ++k) sum += c.forecast[best + k].gPerKwh;
    textf(x + 20, y + 192, F::XS, C::MUTED, td::top_left, "Greenest 3 h in the next day");
    textf(x + 20, y + 208, F::S, C::GREEN, td::top_left, "%s - %s  (~%lu g)", a, b, static_cast<unsigned long>(sum / 6));
  }
}

void mixCard(const CarbonData &c) {
  constexpr int x = 400, y = BODY_Y + M, w = 392, h = 236;
  card(x, y, w, h, "Generation mix - North West");
  int order[static_cast<int>(Fuel::Count)];
  for (int i = 0; i < static_cast<int>(Fuel::Count); ++i) order[i] = i;
  std::sort(order, order + static_cast<int>(Fuel::Count), [&](int a, int b) { return c.mix[a] > c.mix[b]; });
  int ry = y + 40;
  for (int k = 0; k < static_cast<int>(Fuel::Count) && ry < y + h - 22; ++k) {
    const int i = order[k];
    if (c.mix[i] < 0.5f) continue;
    const Fuel f = static_cast<Fuel>(i);
    text(Carbon::fuelName(f), x + 16, ry, F::XS, C::TEXT);
    const int bx = x + 100, bw = w - 100 - 70;
    canvas.fillRoundRect(bx, ry + 2, bw, 12, 6, C::CARD2);
    canvas.fillRoundRect(bx, ry + 2, max(6, static_cast<int>(c.mix[i] / 100.0f * bw)), 12, 6, fuelColor(f));
    textf(x + w - 16, ry, F::XS, C::MUTED, td::top_right, "%.1f%%", c.mix[i]);
    ry += 24;
  }
}

void forecastCard(const CarbonData &c) {
  constexpr int x = M, y = BODY_Y + 252, w = W - 2 * M, h = 120;
  card(x, y, w, h, "Next 48 hours");
  if (c.count == 0) return placeholder(x, y, w, h, "No forecast");
  uint16_t hi = 1;
  for (int i = 0; i < c.count; ++i) hi = max(hi, c.forecast[i].gPerKwh);
  const int cx0 = x + 16, cw = w - 32, cBot = y + h - 22, cH = 62;
  const int bw = max(2, cw / CARBON_SLOTS);
  const int best = c.greenestWindow(6, 48);
  if (best >= 0) canvas.fillRect(cx0 + best * bw - 1, cBot - cH - 4, 6 * bw + 1, cH + 4, rgb(24, 60, 40));
  for (int i = 0; i < c.count; ++i) {
    const int bh = max(2, c.forecast[i].gPerKwh * cH / hi);
    canvas.fillRect(cx0 + i * bw, cBot - bh, bw - 1, bh, carbonColor(c.forecast[i].index));
  }
  canvas.drawFastHLine(cx0, cBot, cw, C::LINE);
  for (int i = 0; i < c.count; i += 12) {  // Every 6 hours.
    char t[6];
    hhmm(c.forecast[i].from, t);
    text(t, cx0 + i * bw, cBot + 3, F::XS, C::DIM);
  }
  textf(x + w - 16, y + 12, F::XS, C::MUTED, td::top_right, "peak %u g", hi);
}

}  // namespace

void pageGrid() {
  const CarbonData &c = DataService::carbon();
  if (!c.valid) {
    placeholder(0, BODY_Y, W, BODY_H, Net::connected() ? "Fetching grid data..." : "Connecting Wi-Fi...");
    return;
  }
  nowCard(c);
  mixCard(c);
  forecastCard(c);
}

}  // namespace UiDraw
