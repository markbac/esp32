/**
 * @file    page_flights.cpp
 * @brief   "Flights" page: tap-to-select radar scope and full flight details.
 *
 * @details The scope is north-up and centred on home, with rings at the full
 *          and half search radius. Every aircraft is drawn rotated to its
 *          track and labelled with its callsign. Tapping an aircraft selects
 *          it (yellow); its route is fetched on demand. Tapping empty space
 *          returns to showing the nearest aircraft.
 */
#include <utility>

#include "config.h"
#include "icons.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

constexpr int CX = 200;                       ///< Scope centre x.
constexpr int CY = BODY_Y + BODY_H / 2;       ///< Scope centre y.
constexpr int R = BODY_H / 2 - 12;            ///< Scope radius = FLIGHT_RADIUS_NM.
constexpr int PX = 408;                       ///< Details card x.
constexpr int HIT_RADIUS = 30;                ///< Tap tolerance, pixels.

/** @brief Screen positions from the last draw, used for hit-testing taps. */
struct Hit {
  char hex[7];
  int x, y;
};
Hit hits[MAX_AIRCRAFT];
int hitCount = 0;

/** @brief Plot position of an aircraft. */
void position(const Aircraft &a, int &x, int &y) {
  const float r = min(a.distNm, static_cast<float>(FLIGHT_RADIUS_NM)) / FLIGHT_RADIUS_NM * R;
  const float b = a.bearing * DEG_TO_RAD;
  x = CX + lroundf(sinf(b) * r);
  y = CY - lroundf(cosf(b) * r);
}

/** @brief Colour by altitude band. */
uint16_t altColor(const Aircraft &a) {
  if (a.onGround) return C::DIM;
  if (a.altFt < 5000) return C::GREEN;
  if (a.altFt < 20000) return C::CYAN;
  return C::TEXT;
}

/** @brief Index of the aircraft shown in the details card. */
int focusIndex(const FlightList &fl) {
  if (state.selectedHex[0]) {
    for (int i = 0; i < fl.count; ++i) {
      if (strcmp(fl.items[i].hex, state.selectedHex) == 0) return i;
    }
  }
  return fl.nearest;
}

void scope(const FlightList &fl, int focus) {
  canvas.fillCircle(CX, CY, R, rgb(14, 22, 32));
  canvas.drawCircle(CX, CY, R, C::LINE);
  canvas.drawCircle(CX, CY, R / 2, C::LINE);
  canvas.drawFastHLine(CX - R, CY, 2 * R, rgb(30, 40, 54));
  canvas.drawFastVLine(CX, CY - R, 2 * R, rgb(30, 40, 54));
  text("N", CX, CY - R + 6, F::S, C::RED, td::top_center);
  textf(CX + 4, CY - R / 2 - 18, F::XS, C::DIM, td::top_left, "%d nm", FLIGHT_RADIUS_NM / 2);
  textf(CX + 4, CY - R - 2, F::XS, C::DIM, td::bottom_left, "%d nm", FLIGHT_RADIUS_NM);
  canvas.fillCircle(CX, CY, 4, C::ACCENT);

  hitCount = 0;
  for (int pass = 0; pass < 2; ++pass) {  // Focused aircraft last, on top.
    for (int i = 0; i < fl.count; ++i) {
      const Aircraft &a = fl.items[i];
      const bool isFocus = i == focus;
      if (isFocus != (pass == 1) || a.distNm > FLIGHT_RADIUS_NM * 1.05f) continue;
      int x, y;
      position(a, x, y);
      if (isFocus) {
        canvas.drawCircle(x, y, 20, C::YELLOW);
        Icons::plane(canvas, x, y, 26, a.track, C::YELLOW);
      } else {
        Icons::plane(canvas, x, y, 16, a.track, altColor(a));
      }
      text(a.callsign[0] ? a.callsign : a.reg, x + 14, y + 8, F::XS, isFocus ? C::YELLOW : C::MUTED);
      if (hitCount < MAX_AIRCRAFT) {
        strlcpy(hits[hitCount].hex, a.hex, sizeof(hits[hitCount].hex));
        hits[hitCount].x = x;
        hits[hitCount].y = y;
        ++hitCount;
      }
    }
  }
  // Altitude key.
  int ky = BODY_Y + BODY_H - 20;
  int kx = 12;
  for (auto &k : {std::make_pair(C::GREEN, "<5k ft"), std::make_pair(C::CYAN, "<20k"), std::make_pair(C::TEXT, "high")}) {
    canvas.fillCircle(kx + 5, ky + 7, 5, k.first);
    kx += 14 + text(k.second, kx + 14, ky, F::XS, C::DIM) + 10;
  }
}

void details(const FlightList &fl, int focus) {
  constexpr int x = PX, y = BODY_Y + M, w = W - PX - M, h = BODY_H - 2 * M;
  const bool selected = state.selectedHex[0] && focus >= 0 && strcmp(fl.items[focus].hex, state.selectedHex) == 0;
  card(x, y, w, h, selected ? "Selected aircraft" : "Nearest aircraft");
  if (focus < 0) {
    placeholder(x, y, w, h, "No aircraft in range");
    return;
  }
  const Aircraft &a = fl.items[focus];
  text(a.callsign[0] ? a.callsign : (a.reg[0] ? a.reg : a.hex), x + 16, y + 34, F::L, C::YELLOW);

  // Route: from the poll (nearest) or the on-demand cache (any aircraft).
  FlightRoute route;
  bool haveRoute = false;
  if (focus == fl.nearest && strcmp(fl.route.callsign, a.callsign) == 0) {
    route = fl.route;
    haveRoute = true;
  } else {
    haveRoute = Flights::findRoute(a.callsign, route);
    if (!haveRoute) Flights::requestRoute(a.callsign);
  }
  int ry = y + 88;
  if (haveRoute && route.known) {
    textf(x + 16, ry, F::M, C::TEXT, td::top_left, "%s  >  %s", route.from, route.to);
    if (route.fromCity[0]) textf(x + 16, ry + 32, F::XS, C::MUTED, td::top_left, "%.22s to %.22s", route.fromCity, route.toCity);
    text(route.airline, x + 16, ry + 52, F::S, C::ACCENT);
  } else {
    text(haveRoute ? "Route unknown" : (a.callsign[0] ? "Looking up route..." : "No callsign"), x + 16, ry, F::S, C::DIM);
  }

  // Aircraft.
  ry = y + 172;
  textf(x + 16, ry, F::S, C::TEXT, td::top_left, "%s  %s", a.type, a.reg);
  if (a.desc[0]) text(a.desc, x + 16, ry + 24, F::XS, C::MUTED);

  // Figures grid.
  auto fig = [&](int col, int row, const char *label, const char *val, uint16_t c) {
    const int fx = x + 16 + col * (w / 2 - 8), fy = y + 230 + row * 56;
    text(label, fx, fy, F::XS, C::MUTED);
    text(val, fx, fy + 16, F::S, c);
  };
  char buf[24];
  if (a.onGround) snprintf(buf, sizeof(buf), "On ground");
  else snprintf(buf, sizeof(buf), "%ld ft", static_cast<long>(a.altFt));
  fig(0, 0, "Altitude", buf, altColor(a));
  if (a.vertRate > 300) snprintf(buf, sizeof(buf), "Climbing %d", a.vertRate);
  else if (a.vertRate < -300) snprintf(buf, sizeof(buf), "Descending %d", -a.vertRate);
  else snprintf(buf, sizeof(buf), "Level");
  fig(1, 0, "Vertical (ft/min)", buf, C::TEXT);
  snprintf(buf, sizeof(buf), "%u kt / %03d", a.speedKt, a.track);
  fig(0, 1, "Speed / track", buf, C::TEXT);
  snprintf(buf, sizeof(buf), "%.1f nm %s", a.distNm, Weather::compass(static_cast<int>(a.bearing)));
  fig(1, 1, "Distance", buf, C::CYAN);

  textf(x + 16, y + h - 24, F::XS, C::DIM, td::top_left, "%u aircraft - tap one on the scope", fl.total);
}

}  // namespace

void pageFlights() {
  const FlightList &fl = DataService::flights();
  if (!fl.valid) {
    placeholder(0, BODY_Y, W, BODY_H, "Scanning for aircraft...");
    return;
  }
  const int focus = focusIndex(fl);
  scope(fl, focus);
  details(fl, focus);
}

bool tapFlights(int x, int y) {
  if (x >= PX) return false;
  int best = -1, bestD = HIT_RADIUS * HIT_RADIUS;
  for (int i = 0; i < hitCount; ++i) {
    const int dx = hits[i].x - x, dy = hits[i].y - y, d = dx * dx + dy * dy;
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  if (best >= 0) {
    strlcpy(state.selectedHex, hits[best].hex, sizeof(state.selectedHex));
    Serial.printf("[flights] selected %s\n", state.selectedHex);
  } else {
    state.selectedHex[0] = '\0';  // Tap on empty sky: back to nearest.
  }
  return true;
}

}  // namespace UiDraw
