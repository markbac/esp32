/**
 * @file    page_flights.cpp
 * @brief   "Flights" page: tap-to-select radar scope with trails, photo and flight details.
 *
 * @details The scope is north-up and centred on home, with rings at the full
 *          and half search radius. Every aircraft is drawn rotated to its
 *          track, labelled with its callsign, with a fading trail of its last
 *          positions (one per poll). Tapping an aircraft selects it (yellow);
 *          its route and photo (planespotters.net) are fetched on demand.
 *          Tapping empty space returns to showing the nearest aircraft.
 *
 *          Controls: "List" switches to a table of every aircraft kept,
 *          nearest first (drag or use the arrows to scroll; tap a row to
 *          select it on the scope). "-" / "+" change the search radius, which
 *          is saved like any other setting and re-polls at once.
 *
 * @verbatim
 *   +--------- scope ---------+ +---- details --------------------------+
 *   |          N              | | [ photo 210x140 ]  EZY98CD            |
 *   |    .  .  ->             | |                    A320  G-EZPX       |
 *   |  .  (+)     ..->        | | (c) photographer   32,975 ft          |
 *   |        <-..             | | EDI > CDG   Edinburgh to Paris        |
 *   |                         | | easyJet                               |
 *   +-------------------------+ | speed/track  vertical  distance       |
 *                               +---------------------------------------+
 * @endverbatim
 */
#include <utility>
#include "log.h"

#include "config.h"
#include "icons.h"
#include "settings.h"
#include "third_party/stb_image.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

constexpr int CX = 200;                       ///< Scope centre x.
constexpr int STRIP_H = 44;                   ///< Control strip above the scope (List, radius).
constexpr int CY = BODY_Y + STRIP_H + (BODY_H - STRIP_H) / 2;  ///< Scope centre y.
constexpr int R = (BODY_H - STRIP_H) / 2 - 8;  ///< Scope radius = the poll's search radius.
constexpr int PX = 408;                       ///< Details card x.
constexpr int HIT_RADIUS = 30;                ///< Tap tolerance, pixels.
constexpr int PHOTO_W = 210, PHOTO_H = 140;   ///< Photo size (420x280 JPEG decoded at 1/2).

/** @brief Screen positions from the last draw, used for hit-testing taps. */
struct Hit {
  char hex[7];
  int x, y;
};
Hit hits[MAX_AIRCRAFT];
int hitCount = 0;

LGFX_Sprite photo;          ///< Decoded photo of the focused aircraft.
char photoHex[7] = "";      ///< Aircraft the sprite holds.
char photoCredit[48] = "";  ///< Photographer credit for it.
int photoState = -1;        ///< 1 = shown, 0 = none available, -1 = loading.

float rangeNm = FLIGHT_RADIUS_NM;  ///< Radius the scope is drawn for (from the poll).

/** @brief Button ids. */
enum Btn : int { BTN_VIEW = 1, BTN_SMALLER, BTN_BIGGER, BTN_UP, BTN_DOWN };

/** @name List view layout */
///@{
constexpr int LIST_TOP = BODY_Y + 72;   ///< First row.
constexpr int ROW_H = 30;
constexpr int ROWS = (TAB_Y - 4 - LIST_TOP) / ROW_H;
///@}
char rowHex[ROWS][7];       ///< Aircraft on each visible row, for taps.
int rowCount = 0;
int listCount = 0;          ///< Aircraft in the list at the last draw (for scroll limits).

/** @brief Screen position for a distance/bearing from home. */
void toScreen(float distNm, float bearing, int &x, int &y) {
  const float r = min(distNm, rangeNm) / rangeNm * R;
  const float b = bearing * DEG_TO_RAD;
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

/** @brief Fading line through the aircraft's previous positions. */
void trail(const Aircraft &a, bool focus) {
  if (a.trailCount == 0) return;
  int px, py;
  toScreen(a.trailDist[0], a.trailBearing[0], px, py);
  for (int k = 1; k <= a.trailCount; ++k) {
    int x, y;
    if (k < a.trailCount) toScreen(a.trailDist[k], a.trailBearing[k], x, y);
    else toScreen(a.distNm, a.bearing, x, y);
    const uint8_t v = 40 + 140 * k / a.trailCount;  // Older = dimmer.
    const uint16_t col = focus ? rgb(v + 60, v + 40, 0) : rgb(v / 3, v / 2, v);
    canvas.drawWideLine(px, py, x, y, focus ? 1.6f : 1.0f, col);
    px = x;
    py = y;
  }
}

void scope(const FlightList &fl, int focus) {
  canvas.fillCircle(CX, CY, R, rgb(14, 22, 32));
  canvas.drawCircle(CX, CY, R, C::LINE);
  canvas.drawCircle(CX, CY, R / 2, C::LINE);
  canvas.drawFastHLine(CX - R, CY, 2 * R, rgb(30, 40, 54));
  canvas.drawFastVLine(CX, CY - R, 2 * R, rgb(30, 40, 54));
  text("N", CX, CY - R + 6, F::S, C::RED, td::top_center);
  textf(CX + 4, CY - R / 2 - 18, F::XS, C::DIM, td::top_left, "%g nm", rangeNm / 2);
  canvas.fillCircle(CX, CY, 4, C::ACCENT);

  for (int i = 0; i < fl.count; ++i) {
    if (!fl.items[i].onGround) trail(fl.items[i], i == focus);
  }

  hitCount = 0;
  for (int pass = 0; pass < 2; ++pass) {  // Focused aircraft last, on top.
    for (int i = 0; i < fl.count; ++i) {
      const Aircraft &a = fl.items[i];
      const bool isFocus = i == focus;
      if (isFocus != (pass == 1) || a.distNm > rangeNm * 1.05f) continue;
      int x, y;
      toScreen(a.distNm, a.bearing, x, y);
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
  int ky = BODY_Y + BODY_H - 20, kx = 12;
  for (auto &k : {std::make_pair(C::GREEN, "<5k ft"), std::make_pair(C::CYAN, "<20k"), std::make_pair(C::TEXT, "high")}) {
    canvas.fillCircle(kx + 5, ky + 7, 5, k.first);
    kx += 14 + text(k.second, kx + 14, ky, F::XS, C::DIM) + 10;
  }
}

/** @brief Radius "-" / "+" buttons, optionally with the setting's value between them. */
void radiusControl(int x, int y, bool label) {
  button(BTN_SMALLER, x, y, 44, 36, "-", false, F::M);
  int bx = x + 52;
  if (label) {
    textf(x + 52 + 30, y + 19, F::S, C::TEXT, td::middle_center, "%u nm", Settings::get().flightRadiusNm);
    bx = x + 52 + 60 + 8;
  }
  button(BTN_BIGGER, bx, y, 44, 36, "+", false, F::M);
}

/** @brief "12,345" into @p buf. */
void thousands(long v, char *buf, size_t n) {
  if (v >= 1000) snprintf(buf, n, "%ld,%03ld", v / 1000, v % 1000);
  else snprintf(buf, n, "%ld", v);
}

/** @brief Table of every aircraft kept, nearest first. */
void list(const FlightList &fl) {
  char buf[48];
  button(BTN_VIEW, M, BODY_Y + 8, 86, 36, "Scope");
  if (fl.count < fl.total) snprintf(buf, sizeof(buf), "%u nearest of %u within %u nm", fl.count, fl.total, fl.radiusNm);
  else snprintf(buf, sizeof(buf), "%u aircraft within %u nm", fl.count, fl.radiusNm);
  text(buf, M + 100, BODY_Y + 27, F::S, C::TEXT, td::middle_left);
  radiusControl(512, BODY_Y + 8, true);
  button(BTN_UP, 688, BODY_Y + 8, 48, 36, nullptr);
  arrow(688 + 24, BODY_Y + 26, true, C::TEXT);
  button(BTN_DOWN, 744, BODY_Y + 8, 48, 36, nullptr);
  arrow(744 + 24, BODY_Y + 26, false, C::TEXT);

  // Column headers.
  const int hy = BODY_Y + 54;
  text("#", 12, hy, F::XS, C::DIM);
  text("Callsign", 40, hy, F::XS, C::MUTED);
  text("Type", 150, hy, F::XS, C::MUTED);
  text("Reg", 215, hy, F::XS, C::MUTED);
  text("Alt ft", 400, hy, F::XS, C::MUTED, td::top_right);
  text("Speed kt", 478, hy, F::XS, C::MUTED, td::top_right);
  text("Vert fpm", 556, hy, F::XS, C::MUTED, td::top_right);
  text("Dist nm", 630, hy, F::XS, C::MUTED, td::top_right);
  text("Bearing", 644, hy, F::XS, C::MUTED);
  text("Route", 722, hy, F::XS, C::MUTED);
  canvas.drawFastHLine(M, LIST_TOP - 4, W - 2 * M, C::LINE);

  listCount = fl.count;
  state.flightsScroll = constrain(state.flightsScroll, 0, max(0, fl.count - ROWS));
  rowCount = 0;
  bool asked = false;  // One route lookup per frame, nearest first.
  for (int r = 0; r < ROWS; ++r) {
    const int i = state.flightsScroll + r;
    if (i >= fl.count) break;
    const Aircraft &a = fl.items[i];
    const int y = LIST_TOP + r * ROW_H;
    const bool sel = state.selectedHex[0] && strcmp(a.hex, state.selectedHex) == 0;
    if (sel) canvas.fillRoundRect(M, y, W - 2 * M, ROW_H - 2, 6, C::CARD2);
    else if (r % 2) canvas.fillRect(M, y, W - 2 * M, ROW_H - 2, C::CARD);
    const int ty = y + (ROW_H - 2) / 2;
    textf(12, ty, F::XS, C::DIM, td::middle_left, "%d", i + 1);
    text(a.callsign[0] ? a.callsign : (a.reg[0] ? a.reg : a.hex), 40, ty, F::S, sel ? C::YELLOW : C::TEXT,
         td::middle_left);
    text(a.type, 150, ty, F::S, C::TEXT, td::middle_left);
    text(a.reg, 215, ty, F::XS, C::MUTED, td::middle_left);
    if (a.onGround) snprintf(buf, sizeof(buf), "ground");
    else thousands(a.altFt, buf, sizeof(buf));
    text(buf, 400, ty, F::S, altColor(a), td::middle_right);
    textf(478, ty, F::S, C::TEXT, td::middle_right, "%u", a.speedKt);
    if (a.vertRate > 300) snprintf(buf, sizeof(buf), "+%d", a.vertRate);
    else if (a.vertRate < -300) snprintf(buf, sizeof(buf), "%d", a.vertRate);
    else snprintf(buf, sizeof(buf), "level");
    text(buf, 556, ty, F::XS, a.vertRate > 300 ? C::GREEN : a.vertRate < -300 ? C::ORANGE : C::DIM, td::middle_right);
    textf(630, ty, F::S, C::CYAN, td::middle_right, "%.1f", a.distNm);
    textf(644, ty, F::XS, C::TEXT, td::middle_left, "%s %03d", Weather::compass(static_cast<int>(a.bearing)),
          static_cast<int>(a.bearing) % 360);
    FlightRoute route;
    if (Flights::findRoute(a.callsign, route)) {
      if (route.known) textf(722, ty, F::XS, C::ACCENT, td::middle_left, "%s>%s", route.from, route.to);
      else text("-", 722, ty, F::XS, C::DIM, td::middle_left);
    } else if (a.callsign[0]) {
      if (!asked) Flights::requestRoute(a.callsign);
      asked = true;
      text("...", 722, ty, F::XS, C::DIM, td::middle_left);
    }
    strlcpy(rowHex[rowCount++], a.hex, sizeof(rowHex[0]));
  }
  if (fl.count == 0) {
    placeholder(0, LIST_TOP, W, TAB_Y - LIST_TOP, "No aircraft in range");
  } else if (fl.count > ROWS) {
    textf(W - M - 4, TAB_Y - 2, F::XS, C::DIM, td::bottom_right, "rows %d-%d of %u - drag to scroll",
          state.flightsScroll + 1, min<int>(state.flightsScroll + ROWS, fl.count), fl.count);
  }
}

/** @brief Decode (once per aircraft) and draw the photo, or a placeholder. */
void photoBox(int x, int y, const Aircraft &a) {
  if (strcmp(photoHex, a.hex) != 0 || photoState == -1) {
    int r = Flights::withPhoto(a.hex, [&](const uint8_t *jpg, size_t len, const char *credit) {
      if (!photo.getBuffer()) {
        photo.setColorDepth(16);
        photo.setPsram(true);
        photo.createSprite(PHOTO_W, PHOTO_H);
      }
      photo.fillScreen(C::CARD2);
      strlcpy(photoCredit, credit, sizeof(photoCredit));
      // planespotters serves progressive JPEGs, so decode with stb_image, then box-scale to fit.
      int w = 0, h = 0, n = 0;
      uint8_t *pix = stbi_load_from_memory(jpg, static_cast<int>(len), &w, &h, &n, 3);
      if (!pix) {
        Log::printf("[photo] decode failed: %s\n", stbi_failure_reason());
        return;
      }
      const float sx = static_cast<float>(w) / PHOTO_W, sy = static_cast<float>(h) / PHOTO_H;
      for (int y = 0; y < PHOTO_H; ++y) {
        for (int x = 0; x < PHOTO_W; ++x) {
          // Average the source block covering this pixel.
          const int x0 = static_cast<int>(x * sx), x1 = max(x0 + 1, static_cast<int>((x + 1) * sx));
          const int y0 = static_cast<int>(y * sy), y1 = max(y0 + 1, static_cast<int>((y + 1) * sy));
          uint32_t r = 0, g = 0, b = 0, k = 0;
          for (int yy = y0; yy < y1 && yy < h; ++yy) {
            const uint8_t *p = pix + (yy * w + x0) * 3;
            for (int xx = x0; xx < x1 && xx < w; ++xx, p += 3, ++k) {
              r += p[0];
              g += p[1];
              b += p[2];
            }
          }
          if (k) photo.drawPixel(x, y, rgb(r / k, g / k, b / k));
        }
      }
      stbi_image_free(pix);
    });
    if (r < 0) Flights::requestPhoto(a.hex);
    strlcpy(photoHex, a.hex, sizeof(photoHex));
    photoState = r;
  }
  if (photoState == 1 && photo.getBuffer()) {
    photo.pushSprite(&canvas, x, y);
    textf(x, y + PHOTO_H + 4, F::XS, C::DIM, td::top_left, "%.34s", photoCredit);
  } else {
    canvas.fillRoundRect(x, y, PHOTO_W, PHOTO_H, 8, C::CARD2);
    Icons::plane(canvas, x + PHOTO_W / 2, y + PHOTO_H / 2 - 10, 40, 45, C::LINE);
    text(photoState == 0 ? "No photo available" : "Loading photo...", x + PHOTO_W / 2, y + PHOTO_H - 26, F::XS, C::DIM,
         td::top_center);
  }
}

void details(const FlightList &fl, int focus) {
  constexpr int x = PX, y = BODY_Y + M, w = W - PX - M, h = BODY_H - 2 * M;
  const bool selected = state.selectedHex[0] && focus >= 0 && strcmp(fl.items[focus].hex, state.selectedHex) == 0;
  card(x, y, w, h);
  if (focus < 0) {
    placeholder(x, y, w, h, "No aircraft in range");
    return;
  }
  const Aircraft &a = fl.items[focus];
  photoBox(x + 12, y + 12, a);

  // Right of the photo: identity and altitude.
  const int rx = x + 12 + PHOTO_W + 12;
  text(selected ? "Selected" : "Nearest", rx, y + 12, F::XS, C::MUTED);
  text(a.callsign[0] ? a.callsign : (a.reg[0] ? a.reg : a.hex), rx, y + 30, F::M, C::YELLOW);
  text(a.type, rx, y + 64, F::S, C::TEXT);
  text(a.reg, rx, y + 88, F::S, C::TEXT);
  char buf[32];
  if (a.onGround) snprintf(buf, sizeof(buf), "On ground");
  else snprintf(buf, sizeof(buf), "%ld ft", static_cast<long>(a.altFt));
  text(buf, rx, y + 116, F::S, altColor(a));
  if (a.squawk[0]) textf(rx, y + 142, F::XS, C::DIM, td::top_left, "sqk %s", a.squawk);

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
  int ry = y + 178;
  if (haveRoute && route.known) {
    textf(x + 16, ry, F::M, C::TEXT, td::top_left, "%s  >  %s", route.from, route.to);
    if (route.fromCity[0]) textf(x + 16, ry + 30, F::XS, C::MUTED, td::top_left, "%.24s to %.24s", route.fromCity, route.toCity);
    text(route.airline, x + 16, ry + 48, F::S, C::ACCENT);
  } else {
    text(haveRoute ? "Route unknown" : (a.callsign[0] ? "Looking up route..." : "No callsign"), x + 16, ry, F::S, C::DIM);
  }
  if (a.desc[0]) textf(x + 16, ry + 74, F::XS, C::MUTED, td::top_left, "%.40s", a.desc);

  // Figures row.
  auto fig = [&](int col, const char *label, const char *val, uint16_t c) {
    const int fx = x + 16 + col * ((w - 32) / 3), fy = y + h - 64;
    text(label, fx, fy, F::XS, C::MUTED);
    text(val, fx, fy + 16, F::S, c);
  };
  snprintf(buf, sizeof(buf), "%u kt %03d", a.speedKt, a.track);
  fig(0, "Speed / track", buf, C::TEXT);
  if (a.vertRate > 300) snprintf(buf, sizeof(buf), "+%d fpm", a.vertRate);
  else if (a.vertRate < -300) snprintf(buf, sizeof(buf), "%d fpm", a.vertRate);
  else snprintf(buf, sizeof(buf), "Level");
  fig(1, "Vertical", buf, C::TEXT);
  snprintf(buf, sizeof(buf), "%.1f nm %s", a.distNm, Weather::compass(static_cast<int>(a.bearing)));
  fig(2, "Distance", buf, C::CYAN);
  textf(x + w - 16, y + h - 20, F::XS, C::DIM, td::top_right, "%u aircraft - tap one", fl.total);
}

}  // namespace

void pageFlights() {
  const FlightList &fl = DataService::flights();
  if (!fl.valid) {
    placeholder(0, BODY_Y, W, BODY_H, "Scanning for aircraft...");
    return;
  }
  rangeNm = fl.radiusNm ? fl.radiusNm : FLIGHT_RADIUS_NM;
  if (state.flightsList) {
    list(fl);
    return;
  }
  const int focus = focusIndex(fl);
  scope(fl, focus);
  details(fl, focus);
  button(BTN_VIEW, M + 4, BODY_Y + 8, 72, 36, "List");
  text("Radius", PX - 8 - 164 - 8, BODY_Y + 26, F::S, C::MUTED, td::middle_right);
  radiusControl(PX - 8 - 164, BODY_Y + 8, true);
}

bool tapFlights(int x, int y) {
  const int b = buttonAt(x, y);
  switch (b) {
    case BTN_VIEW:
      state.flightsList = !state.flightsList;
      return true;
    case BTN_SMALLER:
    case BTN_BIGGER: {
      SettingsData cfg = Settings::get();
      const uint16_t r =
          Settings::step(Settings::FLIGHT_RADII, Settings::FLIGHT_RADII_COUNT, cfg.flightRadiusNm, b == BTN_BIGGER ? 1 : -1);
      if (r != cfg.flightRadiusNm) {
        cfg.flightRadiusNm = r;
        Settings::set(cfg);
        DataService::refresh("flights");
        Log::printf("[flights] radius %u nm\n", r);
      }
      return true;
    }
    case BTN_UP: return dragFlights(ROWS * ROW_H);
    case BTN_DOWN: return dragFlights(-ROWS * ROW_H);
    default: break;
  }
  if (state.flightsList) {
    const int r = (y - LIST_TOP) / ROW_H;
    if (y >= LIST_TOP && r < rowCount) {
      strlcpy(state.selectedHex, rowHex[r], sizeof(state.selectedHex));
      state.flightsList = false;  // Show it on the scope with its photo and details.
      Log::printf("[flights] selected %s from list\n", state.selectedHex);
    }
    return true;
  }
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
    Log::printf("[flights] selected %s\n", state.selectedHex);
  } else {
    state.selectedHex[0] = '\0';  // Tap on empty sky: back to nearest.
  }
  return true;
}

bool dragFlights(int dy) {
  if (!state.flightsList) return false;
  int rows = -dy / ROW_H;
  if (rows == 0) rows = dy < 0 ? 1 : -1;
  state.flightsScroll = constrain(state.flightsScroll + rows, 0, max(0, listCount - ROWS));
  return true;
}

}  // namespace UiDraw
