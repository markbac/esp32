/**
 * @file    page_flights.cpp
 * @brief   Aircraft "radar scope" (left) and nearest-flight details (right).
 *
 * @details The scope is centred on home with north up; rings mark the full
 *          and half search radius. Each aircraft is drawn as an arrow pointing
 *          along its track; the nearest airborne one is highlighted in yellow.
 */
#include "config.h"
#include "icons.h"
#include "theme.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

constexpr int SCOPE_CX = 58;   ///< Scope centre x.
constexpr int SCOPE_CY = 75;   ///< Scope centre y.
constexpr int SCOPE_R = 53;    ///< Scope radius (= FLIGHT_RADIUS_NM).
constexpr int INFO_X = 120;    ///< Left edge of the details panel.

/** @brief Format feet with a thousands separator, e.g. "36,975ft". */
void formatAltitude(const Aircraft &a, char *buf, size_t n) {
  if (a.onGround) snprintf(buf, n, "ground");
  else if (a.altFt >= 1000) snprintf(buf, n, "%ld,%03ldft", static_cast<long>(a.altFt / 1000), static_cast<long>(a.altFt % 1000));
  else snprintf(buf, n, "%ldft", static_cast<long>(a.altFt));
}

/** @brief Range rings, north marker and every aircraft position. */
void scope(const FlightList &fl) {
  canvas.drawCircle(SCOPE_CX, SCOPE_CY, SCOPE_R, Theme::DKGREY);
  canvas.drawCircle(SCOPE_CX, SCOPE_CY, SCOPE_R / 2, Theme::DKGREY);
  canvas.drawFastHLine(SCOPE_CX - SCOPE_R, SCOPE_CY, 2 * SCOPE_R, 0x18C3);
  canvas.drawFastVLine(SCOPE_CX, SCOPE_CY - SCOPE_R, 2 * SCOPE_R, 0x18C3);
  text("N", SCOPE_CX + 3, SCOPE_CY - SCOPE_R + 2, 1, Theme::GREY);
  canvas.fillCircle(SCOPE_CX, SCOPE_CY, 2, Theme::CYAN);

  char buf[8];
  snprintf(buf, sizeof(buf), "%unm", fl.radiusNm);
  text(buf, 1, 120, 1, Theme::GREY);

  // Trails: previous positions (one per poll), drawn under the aircraft.
  auto toXY = [&](float dist, float bearing, int &x, int &y) {
    const float rad = fl.radiusNm, r = min(dist, rad) / rad * SCOPE_R;
    x = SCOPE_CX + lroundf(sinf(bearing * DEG_TO_RAD) * r);
    y = SCOPE_CY - lroundf(cosf(bearing * DEG_TO_RAD) * r);
  };
  for (int i = 0; i < fl.count; ++i) {
    const Aircraft &a = fl.items[i];
    if (a.onGround || a.trailCount == 0) continue;
    int px, py;
    toXY(a.trailDist[0], a.trailBearing[0], px, py);
    for (int k = 1; k <= a.trailCount; ++k) {
      int x, y;
      if (k < a.trailCount) toXY(a.trailDist[k], a.trailBearing[k], x, y);
      else toXY(a.distNm, a.bearing, x, y);
      canvas.drawLine(px, py, x, y, i == fl.nearest ? 0x8400 /* dark yellow */ : Theme::DKGREY);
      px = x;
      py = y;
    }
  }

  // Others first, nearest last so it draws on top.
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < fl.count; ++i) {
      const Aircraft &a = fl.items[i];
      const bool isNearest = (i == fl.nearest);
      if (a.onGround || isNearest != (pass == 1) || a.distNm > fl.radiusNm) continue;
      const float r = a.distNm / fl.radiusNm * SCOPE_R;
      const float b = a.bearing * DEG_TO_RAD;
      const int x = SCOPE_CX + lroundf(sinf(b) * r), y = SCOPE_CY - lroundf(cosf(b) * r);
      Icons::plane(canvas, x, y, isNearest ? 14 : 8, a.track, isNearest ? Theme::YELLOW : Theme::WHITE);
    }
  }
}

/** @brief Callsign, route, airline, type, altitude, speed, distance. */
void details(const FlightList &fl) {
  char buf[32];
  if (fl.nearest < 0) {
    centered("No aircraft", INFO_X + 60, 60, 1, Theme::LIGHT);
    snprintf(buf, sizeof(buf), "within %unm", fl.radiusNm);
    centered(buf, INFO_X + 60, 72, 1, Theme::GREY);
    return;
  }
  const Aircraft &a = fl.items[fl.nearest];
  const FlightRoute &route = fl.route;

  text(a.callsign[0] ? a.callsign : a.reg, INFO_X, 21, 2, Theme::YELLOW);
  if (route.known && route.from[0] && route.to[0]) {
    snprintf(buf, sizeof(buf), "%s>%s", route.from, route.to);
    text(buf, INFO_X, 40, 2, Theme::WHITE);
  } else {
    text("route ?", INFO_X, 40, 2, Theme::GREY);
  }
  snprintf(buf, sizeof(buf), "%.20s", route.known ? route.airline : "");
  text(buf, INFO_X, 58, 1, Theme::LIGHT);

  snprintf(buf, sizeof(buf), "%s %s", a.type, a.reg);
  text(buf, INFO_X, 70, 1, Theme::LIGHT);
  formatAltitude(a, buf, sizeof(buf));
  text(buf, INFO_X, 82, 1, Theme::WHITE);
  snprintf(buf, sizeof(buf), "%ukt hdg %03d", a.speedKt, a.track);
  text(buf, INFO_X, 94, 1, Theme::WHITE);
  snprintf(buf, sizeof(buf), "%.1fnm %s", a.distNm, Weather::compass(static_cast<int>(a.bearing)));
  text(buf, INFO_X, 106, 1, Theme::CYAN);
  snprintf(buf, sizeof(buf), "%u aircraft", fl.total);
  text(buf, INFO_X, 118, 1, Theme::GREY);
}

}  // namespace

void pageFlights(const UiModel &m) {
  const FlightList &fl = m.flights;
  if (!fl.valid) return waiting("Scanning for aircraft...");
  scope(fl);
  details(fl);
}

}  // namespace UiDraw
