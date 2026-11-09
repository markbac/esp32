/**
 * @file    page_lab.cpp
 * @brief   "Lab" page: NAS health (Glances), internet speed, service monitors, Sonos.
 *
 * @verbatim
 *   +-- NAS ----------------------------+ +-- Internet ----------------------+
 *   | NASHome  up 11 days               | |  93 down  23 up  20 ms         |
 *   | CPU  [=====       ]  6 %           | +-- Sonos -----------------------+
 *   | Mem  [==========  ] 56 %           | | Bar      PLAYING  Song - Artist |
 *   | Disk [========    ] 59 % of 5.8 TB | | Kitchen  STOPPED               |
 *   | Temp 48 C   load 105 %            | |                                |
 *   | 28/29 containers healthy          | |                                |
 *   +-----------------------------------+ +--------------------------------+
 *   +-- Services (Uptime Kuma) ---------------------------------------------+
 *   | 16 / 21 up    [Flame] [Heimdall] [NetAlertX] ...                     |
 *   +-----------------------------------------------------------------------+
 * @endverbatim
 */
#include "config.h"
#include "connectivity.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

/** @brief Label, percentage bar and value. Bar turns orange/red past the thresholds. */
void gauge(int x, int y, int w, const char *label, float pct, const char *value, float warn = 75, float crit = 90) {
  text(label, x, y, F::XS, C::MUTED);
  const int bx = x + 64, bw = w - 64 - 110;
  canvas.fillRoundRect(bx, y + 2, bw, 14, 7, C::CARD2);
  const uint16_t col = pct >= crit ? C::RED : pct >= warn ? C::ORANGE : C::GREEN;
  canvas.fillRoundRect(bx, y + 2, max(8, static_cast<int>(constrain(pct, 0.0f, 100.0f) / 100.0f * bw)), 14, 7, col);
  text(value, x + w, y - 2, F::S, C::TEXT, td::top_right);
}

void nasCard(const NasStatus &n) {
  constexpr int x = M, y = BODY_Y + M, w = 392, h = 236;
  card(x, y, w, h, "NAS");
  if (!n.ok) return placeholder(x, y, w, h, "Glances not reachable");
  textf(x + 70, y + 10, F::S, C::TEXT, td::top_left, "%s", n.host);
  textf(x + w - 14, y + 13, F::XS, C::DIM, td::top_right, "up %s", n.uptime);
  char buf[32];
  const int gx = x + 16, gw = w - 32;
  snprintf(buf, sizeof(buf), "%.0f %%", n.cpu);
  gauge(gx, y + 44, gw, "CPU", n.cpu, buf);
  snprintf(buf, sizeof(buf), "%.0f %%", n.mem);
  gauge(gx, y + 74, gw, "Memory", n.mem, buf, 85, 95);
  snprintf(buf, sizeof(buf), "%.0f%% %.1fTB", n.disk, n.diskTb);
  gauge(gx, y + 104, gw, "Disk", n.disk, buf, 80, 90);
  if (!isnan(n.temp)) {
    snprintf(buf, sizeof(buf), "%.0f C", n.temp);
    gauge(gx, y + 134, gw, "CPU temp", n.temp, buf, 70, 80);  // Scaled 0-100 C.
  }
  textf(gx, y + 166, F::XS, C::MUTED, td::top_left, "Load %.0f%% of %s", n.load, n.cpuName);
  const uint16_t cc = n.unhealthy ? C::ORANGE : C::GREEN;
  textf(gx, y + 190, F::S, cc, td::top_left, "%u / %u containers healthy", n.healthy, n.containers);
  if (n.unhealthy) textf(gx, y + 214, F::XS, C::ORANGE, td::top_left, "%.48s", n.unhealthyNames);
}

void speedCard(const SpeedStatus &s) {
  constexpr int x = 400, y = BODY_Y + M, w = 392, h = 110;
  card(x, y, w, h, "Internet");
  if (!s.ok) return placeholder(x, y, w, h, "Speedtest Tracker not reachable");
  if (s.failed) {
    text("Last speed test failed", x + 16, y + 44, F::M, C::ORANGE);
    if (s.testedAt) {
      char t[6];
      hhmm(s.testedAt, t);
      textf(x + 16, y + 78, F::XS, C::MUTED, td::top_left, "Speedtest Tracker run at %s", t);
    }
    return;
  }
  const int cw = (w - 32) / 3;
  auto fig = [&](int i, float v, const char *unit, const char *label, uint16_t col) {
    const int fx = x + 16 + i * cw;
    textf(fx, y + 36, F::L, col, td::top_left, "%.0f", v);
    textf(fx, y + 80, F::XS, C::MUTED, td::top_left, "%s %s", unit, label);
  };
  fig(0, s.down, "Mbps", "down", C::GREEN);
  fig(1, s.up, "Mbps", "up", C::CYAN);
  fig(2, s.ping, "ms", "ping", C::YELLOW);
  if (s.testedAt) {
    char t[6];
    hhmm(s.testedAt, t);
    textf(x + w - 14, y + 13, F::XS, C::DIM, td::top_right, "tested %s", t);
  }
}

void sonosCard(const HomelabData &d) {
  constexpr int x = 400, y = BODY_Y + 126, w = 392, h = 118;
  card(x, y, w, h, "Sonos");
  int ry = y + 38;
  for (uint8_t i = 0; i < d.sonosCount; ++i, ry += 38) {
    const SonosStatus &s = d.sonos[i];
    if (!s.ok) {
      text("unreachable", x + 16, ry, F::XS, C::DIM);
      continue;
    }
    const bool playing = !strcmp(s.state, "PLAYING");
    canvas.fillCircle(x + 22, ry + 9, 6, playing ? C::GREEN : C::LINE);
    text(s.room, x + 36, ry, F::S, C::TEXT);
    if (playing && s.title[0]) {
      textf(x + 150, ry, F::XS, C::TEXT, td::top_left, "%.28s", s.title);
      textf(x + 150, ry + 16, F::XS, C::MUTED, td::top_left, "%.28s", s.artist);
    } else {
      text(playing ? "Playing" : (!strcmp(s.state, "PAUSED_PLAYBACK") ? "Paused" : "Idle"), x + 150, ry + 2, F::XS, C::MUTED);
    }
  }
}

void servicesCard(const ServicesStatus &s) {
  constexpr int x = M, y = BODY_Y + 252, w = W - 2 * M, h = 120;
  card(x, y, w, h, "Services - Uptime Kuma");
  if (!s.ok) return placeholder(x, y, w, h, "Uptime Kuma status page not reachable");
  textf(x + 16, y + 40, F::L, s.down ? C::ORANGE : C::GREEN, td::top_left, "%u / %u", s.up, s.total);
  text("up", x + 16, y + 88, F::XS, C::MUTED);
  if (!s.down) {
    text("All services healthy", x + 190, y + 50, F::M, C::GREEN);
    return;
  }
  // Down services as red chips, wrapping onto a second row.
  int cx = x + 190, cy = y + 40;
  char name[40];
  const char *p = s.downNames;
  while (*p) {
    const char *comma = strstr(p, ", ");
    const size_t len = comma ? static_cast<size_t>(comma - p) : strlen(p);
    snprintf(name, sizeof(name), "%.*s", static_cast<int>(min<size_t>(len, 30)), p);
    const int cw = textWidth(name, F::XS) + 20;
    if (cx + cw > x + w - 12) {
      cx = x + 190;
      cy += 34;
      if (cy > y + h - 30) break;
    }
    chip(cx, cy, name, rgb(110, 30, 30), C::TEXT, F::XS);
    cx += cw + 8;
    p += len + (comma ? 2 : 0);
  }
}

}  // namespace

void pageLab() {
  const HomelabData &d = DataService::homelab();
  if (!d.valid) {
    placeholder(0, BODY_Y, W, BODY_H, Net::connected() ? "Contacting home-lab services..." : "Connecting Wi-Fi...");
    return;
  }
  nasCard(d.nas);
  speedCard(d.speed);
  sonosCard(d);
  servicesCard(d.services);
}

}  // namespace UiDraw
