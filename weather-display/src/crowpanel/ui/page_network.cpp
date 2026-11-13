/**
 * @file    page_network.cpp
 * @brief   Network section: Signal, Test, Monitor and Help pages.
 *
 * @details
 *  - **Signal**: live signal of the joined access point (updated every
 *    second, so the panel works as a walk-around meter on a USB power bank),
 *    every network in range from a scan each minute, and a 3-hour signal
 *    history graph. Tap a network to highlight it on the graph.
 *  - **Test**: a 30-second check (NetTest::start): 20 pings to the router,
 *    20 to the internet, a DNS lookup and an 8-second download, with a
 *    plain-English diagnosis.
 *  - **Monitor**: watches the connection for 15-120 minutes
 *    (NetTest::startMonitor): pings every second, one log entry per minute,
 *    drawn as a timeline of ping times, lost pings and drop-outs.
 *  - **Help**: what each figure means and how to use the tests.
 *
 *  Results also go to the SD card (/logs/net-YYYY-MM-DD.csv) and the web
 *  page /logs, where tests can be started from a phone.
 */
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <new>
#include <utility>

#include "connectivity.h"
#include "log.h"
#include "net_test.h"
#include "ui_draw.h"
#include "wifi_scan.h"

namespace UiDraw {
namespace {

enum Btn : int { BTN_RUN = 1, BTN_UP, BTN_DOWN, BTN_MON_START, BTN_MIN_BASE = 10 };

const uint8_t MONITOR_CHOICES[] = {15, 30, 60, 120};  ///< Monitor lengths offered, minutes.

constexpr int BOTTOM = BODY_Y + BODY_H - M;  ///< Lowest card edge.
constexpr int LX = M, LY = BODY_Y + M, LW = 384, LH = BODY_H - 2 * M;
constexpr int RX = LX + LW + M, RW = W - RX - M;

// Signal page.
constexpr int CY0 = BODY_Y + M, CH = 196;
constexpr int GY = CY0 + CH + M, GH = BOTTOM - GY;
constexpr int ROW_TOP = LY + 64, ROW_H = 26;
constexpr int ROWS = (LY + LH - 26 - ROW_TOP) / ROW_H;
constexpr int GRAPHED = 6;
const uint16_t SERIES[GRAPHED] = {C::ACCENT, C::GREEN, C::ORANGE, C::PURPLE, C::CYAN, C::YELLOW};

WifiScanData *snap = nullptr;  ///< Page copy of the survey (PSRAM).
MonitorData *mon = nullptr;    ///< Page copy of the monitor log (PSRAM).
uint8_t order[WIFI_MAX_NETS];  ///< Indices into snap->nets, strongest first.
uint8_t rowNet[ROWS];
int rowCount = 0, listCount = 0;

/** @brief Allocate an object in PSRAM once. */
template <typename T>
T *psram(T *&p) {
  if (!p) {
    void *mem = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mem) p = new (mem) T();
  }
  return p;
}

uint16_t rssiColor(int r) { return r >= -60 ? C::GREEN : r >= -70 ? C::YELLOW : r >= -80 ? C::ORANGE : C::RED; }
const char *rssiWord(int r) { return r >= -60 ? "Strong" : r >= -70 ? "Good" : r >= -80 ? "Weak" : "Very weak"; }
int rssiBars(int r) { return r >= -55 ? 4 : r >= -65 ? 3 : r >= -75 ? 2 : r >= -85 ? 1 : 0; }
uint16_t msColor(float ms, float ok, float bad) { return ms > bad ? C::RED : ms > ok ? C::ORANGE : C::GREEN; }

/**
 * @brief Draw @p s word-wrapped to width @p w.
 * @return y below the last line.
 */
int para(const char *s, int x, int y, int w, F f = F::XS, uint16_t col = C::MUTED, int lineH = 17) {
  char line[96] = "";
  const char *p = s;
  while (*p) {
    const char *sp = strchr(p, ' ');
    const size_t wl = sp ? static_cast<size_t>(sp - p) : strlen(p);
    char trial[200];
    snprintf(trial, sizeof(trial), "%s%s%.*s", line, line[0] ? " " : "", static_cast<int>(wl), p);
    if (line[0] && textWidth(trial, f) > w) {
      text(line, x, y, f, col);
      y += lineH;
      snprintf(line, sizeof(line), "%.*s", static_cast<int>(wl), p);
    } else {
      strlcpy(line, trial, sizeof(line));
    }
    p += wl;
    while (*p == ' ') ++p;
  }
  if (line[0]) {
    text(line, x, y, f, col);
    y += lineH;
  }
  return y;
}

bool selected(const WifiNet &n) {
  static const uint8_t none[6] = {};
  return memcmp(state.wifiBssid, none, 6) != 0 && memcmp(state.wifiBssid, n.bssid, 6) == 0;
}

void sortNets() {
  const WifiScanData &d = *snap;
  for (uint8_t i = 0; i < d.count; ++i) order[i] = i;
  std::sort(order, order + d.count, [&](uint8_t a, uint8_t b) {
    const WifiNet &x = d.nets[a], &y = d.nets[b];
    const int kx = x.rssi != WIFI_NOT_SEEN ? 1000 + x.rssi : x.best;
    const int ky = y.rssi != WIFI_NOT_SEEN ? 1000 + y.rssi : y.best;
    return kx > ky;
  });
}

// ================================================================ Signal

void listCard() {
  const WifiScanData &d = *snap;
  card(LX, LY, LW, LH);
  textf(LX + 14, LY + 10, F::S, C::MUTED, td::top_left, "Networks in range (%u)", d.count);
  button(BTN_UP, LX + LW - 108, LY + 6, 46, 32, nullptr);
  arrow(LX + LW - 85, LY + 22, true, C::TEXT);
  button(BTN_DOWN, LX + LW - 56, LY + 6, 46, 32, nullptr);
  arrow(LX + LW - 33, LY + 22, false, C::TEXT);
  const int hy = LY + 44;
  text("Name", LX + 34, hy, F::XS, C::DIM);
  text("Ch", LX + 222, hy, F::XS, C::DIM, td::top_right);
  text("Security", LX + 232, hy, F::XS, C::DIM);
  text("dBm", LX + LW - 14, hy, F::XS, C::DIM, td::top_right);

  listCount = d.count;
  rowCount = 0;
  if (!d.scans) {
    placeholder(LX, LY, LW, LH, "First scan in a few seconds...");
    return;
  }
  state.wifiScroll = constrain(state.wifiScroll, 0, max(0, d.count - ROWS));
  for (int r = 0; r < ROWS; ++r) {
    const int k = state.wifiScroll + r;
    if (k >= d.count) break;
    const WifiNet &n = d.nets[order[k]];
    const int y = ROW_TOP + r * ROW_H, ty = y + ROW_H / 2 - 1;
    const bool seen = n.rssi != WIFI_NOT_SEEN;
    if (selected(n)) canvas.fillRoundRect(LX + 4, y, LW - 8, ROW_H - 2, 6, C::CARD2);
    if (k < GRAPHED) canvas.fillRoundRect(LX + 12, ty - 5, 12, 10, 3, SERIES[k]);
    char name[36];
    snprintf(name, sizeof(name), "%.17s%s", n.ssid[0] ? n.ssid : "(hidden)", n.connected ? " *" : "");
    text(name, LX + 34, ty, F::XS, !seen ? C::DIM : n.connected ? C::ACCENT : C::TEXT, td::middle_left);
    textf(LX + 222, ty, F::XS, C::MUTED, td::middle_right, "%u", n.channel);
    text(WifiScan::authName(n.auth), LX + 232, ty, F::XS, n.auth == 0 ? C::ORANGE : C::DIM, td::middle_left);
    if (seen) {
      wifiBars(LX + LW - 86, ty - 9, rssiBars(n.rssi), true);
      textf(LX + LW - 14, ty, F::S, rssiColor(n.rssi), td::middle_right, "%d", n.rssi);
    } else {
      text("gone", LX + LW - 14, ty, F::XS, C::DIM, td::middle_right);
    }
    rowNet[rowCount++] = order[k];
  }
  char buf[24];
  ago(d.lastScanMs, buf, sizeof(buf));
  const bool paused = NetTest::running() || NetTest::monitoring();
  textf(LX + 14, LY + LH - 8, F::XS, paused ? C::ORANGE : C::DIM, td::bottom_left, "%s %s. * = connected. Tap to graph.",
        paused ? "Scans paused during a test; last" : "Scanned every minute; last", buf);
}

/** @brief Signal scale -90..-40 dBm with a marker at @p r. */
void scale(int x, int y, int w, int r) {
  const int stops[] = {-90, -80, -70, -60, -40};
  const uint16_t cols[] = {C::RED, C::ORANGE, C::YELLOW, C::GREEN};
  auto px = [&](int db) { return x + (constrain(db, -90, -40) + 90) * w / 50; };
  for (int i = 0; i < 4; ++i) canvas.fillRect(px(stops[i]), y, px(stops[i + 1]) - px(stops[i]), 8, cols[i]);
  for (int i = 0; i < 4; ++i) textf(px(stops[i]), y + 11, F::XS, C::DIM, td::top_center, "%d", stops[i]);
  const int mx = px(r);
  canvas.fillTriangle(mx, y - 2, mx - 6, y - 10, mx + 6, y - 10, C::TEXT);
}

void connectionCard() {
  card(RX, CY0, RW, CH, "This connection (live)");
  if (!Net::connected()) {
    placeholder(RX, CY0, RW, CH, "Not connected to Wi-Fi");
    return;
  }
  const int r = Net::rssi();
  text(Net::ssid().c_str(), RX + RW - 14, CY0 + 10, F::S, C::ACCENT, td::top_right);
  const int w = textf(RX + 14, CY0 + 38, F::L, rssiColor(r), td::top_left, "%d", r);
  text("dBm", RX + 20 + w, CY0 + 60, F::S, C::MUTED);
  text(rssiWord(r), RX + 90 + w, CY0 + 42, F::S, rssiColor(r));
  textf(RX + 90 + w, CY0 + 66, F::XS, C::DIM, td::top_left, "ch %d  %s", static_cast<int>(WiFi.channel()),
        WiFi.BSSIDstr().c_str());
  scale(RX + 24, CY0 + 104, RW - 48, r);
  para("Updates every second: run the panel from a USB power bank and carry it to a room to measure the signal "
       "there. Closer to 0 is better.",
       RX + 14, CY0 + 136, RW - 28);
}

void signalGraph() {
  const WifiScanData &d = *snap;
  card(RX, GY, RW, GH);
  textf(RX + 14, GY + 8, F::XS, C::MUTED, td::top_left, "Signal history, %u min (top 6 networks)",
        static_cast<unsigned>(d.samples * WIFI_SCAN_INTERVAL_MS / 60000));
  const int gx = RX + 44, gy = GY + 28, gw = RW - 56, gh = GH - 44;
  constexpr int TOP = -30, BOT = -95;
  for (int db = -40; db >= -90; db -= 10) {
    const int y = gy + (TOP - db) * gh / (TOP - BOT);
    canvas.drawFastHLine(gx, y, gw, C::CARD2);
    textf(gx - 6, y, F::XS, C::DIM, td::middle_right, "%d", db);
  }
  text("now", gx + gw, gy + gh + 2, F::XS, C::DIM, td::top_right);
  if (d.samples < 2) {
    text("Builds up over the next few minutes", gx + gw / 2, gy + gh / 2, F::XS, C::DIM, td::middle_center);
    return;
  }
  const int span = WIFI_HISTORY - 1;
  auto px = [&](int back) { return gx + gw - back * gw / span; };
  auto py = [&](int db) { return gy + constrain((TOP - db) * gh / (TOP - BOT), 0, gh); };
  bool anySel = false;
  for (int k = 0; k < min<int>(GRAPHED, d.count); ++k) anySel |= selected(d.nets[order[k]]);
  for (int pass = 0; pass < 2; ++pass) {
    for (int k = min<int>(GRAPHED, d.count) - 1; k >= 0; --k) {
      const uint8_t i = order[k];
      const bool sel = selected(d.nets[i]);
      if (sel != (pass == 1)) continue;
      const uint16_t col = anySel && !sel ? C::LINE : SERIES[k];
      int lx = -1, ly = 0;
      for (int b = d.samples - 1; b >= 0; --b) {
        const int8_t v = d.at(i, b);
        if (v == WIFI_NOT_SEEN) {
          lx = -1;
          continue;
        }
        const int x = px(b), y = py(v);
        if (lx >= 0) canvas.drawWideLine(lx, ly, x, y, sel ? 2.0f : 1.0f, col);
        else canvas.fillCircle(x, y, 1, col);
        lx = x;
        ly = y;
      }
    }
  }
}

// ================================================================ Test

/** @brief Numbered step: title and wrapped description; returns the next y. */
int step(int n, const char *title, const char *desc, int x, int y, int w) {
  canvas.fillCircle(x + 11, y + 10, 11, C::CARD2);
  textf(x + 11, y + 10, F::XS, C::ACCENT, td::middle_center, "%d", n);
  text(title, x + 30, y + 1, F::S, C::TEXT);
  return para(desc, x + 30, y + 24, w - 30) + 6;
}

/** @brief Plain-English reading of a finished test. */
const char *diagnose(const NetTestResult &t) {
  const bool routerBad = !t.router.received || t.router.lossPct() >= 3 || t.router.avgMs > 30 || t.router.jitterMs > 20;
  const bool inetBad = !t.internet.received || t.internet.lossPct() >= 3 || t.internet.avgMs > 80 || t.internet.jitterMs > 30;
  if (routerBad && t.rssi < -72)
    return "The Wi-Fi signal here is weak and the link to the router is laggy. Move closer to the router, or add a "
           "mesh point / extender near this room.";
  if (routerBad)
    return "The link to the router is laggy even though the signal is OK: the Wi-Fi channel is likely busy or "
           "congested. Try another channel, or a wired / mesh connection.";
  if (inetBad)
    return "Wi-Fi is fine but the internet connection is slow or dropping: the broadband line or something using "
           "lots of bandwidth (downloads, streaming, backups).";
  if (t.rssi < -72) return "Works now, but the signal is weak here, so it may glitch when the house is busy.";
  return "Looks healthy here. If problems come and go, run a Monitor while they happen.";
}

void testExplain() {
  card(LX, LY, LW, LH, "What this test does (about 30 s)");
  int y = LY + 42;
  y = step(1, "Router ping x20", "Round trips to your router: measures this room's Wi-Fi link on its own.", LX + 14, y,
           LW - 28);
  y = step(2, "Internet ping x20", "Round trips to 1.1.1.1 (Cloudflare): adds your broadband line.", LX + 14, y, LW - 28);
  y = step(3, "DNS lookup", "Time to look up a web address. Slow DNS makes pages slow to start.", LX + 14, y, LW - 28);
  step(4, "Download 8 s", "From speed.cloudflare.com. This panel's own speed (about 1-10 Mbps max): compare rooms, "
       "not with your broadband.", LX + 14, y, LW - 28);
}

/** @brief Result row: label, main value (coloured), detail. */
void resultRow(int y, const char *label, const char *value, uint16_t col, const char *detail) {
  text(label, RX + 14, y, F::XS, C::MUTED);
  text(value, RX + 120, y - 3, F::S, col);
  text(detail, RX + RW - 14, y, F::XS, C::DIM, td::top_right);
}

void testResults() {
  card(RX, LY, RW, LH, "Results");
  const NetTestResult t = NetTest::get();
  const bool run = t.state == NetTestResult::State::Running;
  const bool monRun = NetTest::monitoring();
  button(BTN_RUN, RX + RW - 136, LY + 6, 124, 36, run ? "Running..." : "Run test", run);
  if (monRun) {
    para("A Monitor is running. Stop it on the Monitor tab to run this test.", RX + 14, LY + 60, RW - 28, F::S, C::ORANGE,
         24);
    return;
  }
  if (t.state == NetTestResult::State::Idle) {
    para("Tap Run test in the room where the Wi-Fi is slow or laggy. The screen stays here while it runs.", RX + 14,
         LY + 60, RW - 28, F::S, C::DIM, 24);
    return;
  }
  char v[32], d[48];
  int y = LY + 54;
  snprintf(v, sizeof(v), "%d dBm", t.rssi);
  resultRow(y, "Signal", v, rssiColor(t.rssi), rssiWord(t.rssi));
  y += 34;
  auto pingRow = [&](const char *label, const PingStats &p, float okMs, float badMs) {
    if (run && !p.sent) {
      resultRow(y, label, "...", C::DIM, "waiting");
    } else if (!p.received) {
      resultRow(y, label, "no replies", C::RED, "all pings lost");
    } else {
      snprintf(v, sizeof(v), "%.0f ms", p.avgMs);
      snprintf(d, sizeof(d), "max %.0f, jitter %.0f, lost %u/%u", p.maxMs, p.jitterMs, static_cast<unsigned>(p.sent - p.received), p.sent);
      resultRow(y, label, v, p.lossPct() > 0 ? C::RED : msColor(max(p.avgMs, p.jitterMs), okMs, badMs), d);
    }
    y += 34;
  };
  pingRow("Router", t.router, 10, 30);
  pingRow("Internet", t.internet, 40, 100);
  if (run) {
    canvas.fillRoundRect(RX + 14, y + 4, RW - 28, 12, 6, C::CARD2);
    canvas.fillRoundRect(RX + 14, y + 4, max(12, (RW - 28) * t.progress / 100), 12, 6, C::ACCENT);
    textf(RX + 14, y + 24, F::XS, C::DIM, td::top_left, "%s...", t.stage);
    return;
  }
  snprintf(v, sizeof(v), t.dnsMs >= 0 ? "%.0f ms" : "failed", t.dnsMs);
  resultRow(y, "DNS", v, t.dnsMs < 0 ? C::RED : msColor(t.dnsMs, 60, 200), "web address lookup");
  y += 34;
  snprintf(v, sizeof(v), t.downMbps >= 0 ? "%.1f Mbps" : "failed", t.downMbps);
  snprintf(d, sizeof(d), "%lu KB in 8 s, this panel", static_cast<unsigned long>(t.downBytes / 1024));
  resultRow(y, "Download", v, t.downMbps < 0 ? C::RED : C::TEXT, d);
  y += 36;
  const char *vd = t.verdict();
  const uint16_t vc = vd[0] == 'G' ? C::GREEN : vd[0] == 'O' ? C::YELLOW : vd[0] == 'P' ? C::ORANGE : C::RED;
  chip(RX + 14, y, vd, vc, C::BG, F::S);
  char a[16];
  ago(t.finishedAtMs, a, sizeof(a));
  textf(RX + RW - 14, y + 6, F::XS, C::DIM, td::top_right, "%s%s", a, Log::sdReady() ? ", saved to SD" : "");
  para(diagnose(t), RX + 14, y + 36, RW - 28, F::XS, C::TEXT);
}

// ================================================================ Monitor

/** @brief Per-minute timeline in the given box. */
void timeline(int x, int y, int w, int h) {
  const MonitorData &m = *mon;
  const int gx = x + 44, gy = y + 30, gw = w - 58, gh = h - 50;
  int lx = x + w - 14;
  for (auto &k : {std::make_pair(C::RED, "lost ping / drop-out"), std::make_pair(rgb(110, 70, 30), "spike >200 ms"),
                  std::make_pair(C::PURPLE, "internet"), std::make_pair(C::ACCENT, "router")}) {
    lx -= textWidth(k.second, F::XS);
    text(k.second, lx, y + 8, F::XS, C::DIM);
    canvas.fillRect(lx - 12, y + 12, 8, 6, k.first);
    lx -= 22;
  }
  text("Ping per minute (ms)", x + 14, y + 8, F::XS, C::MUTED);
  constexpr int TOP_MS = 300;
  for (int ms = 0; ms <= TOP_MS; ms += 100) {
    const int yy = gy + gh - ms * gh / TOP_MS;
    canvas.drawFastHLine(gx, yy, gw, C::CARD2);
    textf(gx - 6, yy, F::XS, C::DIM, td::middle_right, "%d", ms);
  }
  const int slots = max<int>(m.durationMin, 1);
  const int bw = max(1, gw / slots - 1);
  auto yFor = [&](int ms) { return gy + gh - min(ms, TOP_MS) * gh / TOP_MS; };
  int prx = -1, pry = 0, pix = -1, piy = 0;
  for (int i = 0; i < m.count; ++i) {
    const MonitorMinute &e = m.log[i];
    const int xx = gx + i * gw / slots;
    if (e.dropouts) canvas.fillRect(xx, gy, bw, gh, rgb(90, 30, 30));
    const int top = yFor(max(e.routerMax, e.inetMax));
    canvas.fillRect(xx, top, bw, gy + gh - top, e.spikes ? rgb(110, 70, 30) : C::CARD2);
    if (e.routerLost || e.inetLost) canvas.fillRect(xx, gy - 5, bw, 4, C::RED);
    const int cx = xx + bw / 2;
    if (e.routerSent > e.routerLost) {
      const int yy = yFor(e.routerAvg);
      if (prx >= 0) canvas.drawWideLine(prx, pry, cx, yy, 1.5f, C::ACCENT);
      prx = cx;
      pry = yy;
    }
    if (e.inetSent > e.inetLost) {
      const int yy = yFor(e.inetAvg);
      if (pix >= 0) canvas.drawWideLine(pix, piy, cx, yy, 1.5f, C::PURPLE);
      pix = cx;
      piy = yy;
    }
  }
  if (m.running) canvas.drawFastVLine(gx + min<int>(m.count, slots) * gw / slots, gy, gh, C::DIM);
  if (m.count) {
    char hm[6];
    hhmm(m.log[0].at, hm);
    text(hm, gx, gy + gh + 4, F::XS, C::DIM);
  } else {
    text("0", gx, gy + gh + 4, F::XS, C::DIM);
  }
  textf(gx + gw, gy + gh + 4, F::XS, C::DIM, td::top_right, "+%u min", m.durationMin);
}

void monitorPage() {
  const MonitorData &m = *mon;
  const bool run = m.running;
  // Controls.
  text("Watch for", M + 6, BODY_Y + 27, F::S, C::MUTED, td::middle_left);  // ~90 px wide.
  for (int i = 0; i < 4; ++i) {
    char l[8];
    snprintf(l, sizeof(l), MONITOR_CHOICES[i] < 120 ? "%u min" : "2 h", MONITOR_CHOICES[i]);
    button(BTN_MIN_BASE + i, 112 + i * 82, BODY_Y + 8, 76, 38, l, state.monitorMins == MONITOR_CHOICES[i], F::XS);
  }
  button(BTN_MON_START, W - M - 130, BODY_Y + 8, 130, 38, run ? "Stop" : "Start", !run);
  char buf[64];
  if (run) {
    snprintf(buf, sizeof(buf), "Running: %lu of %u min",
             static_cast<unsigned long>((millis() - m.startedAtMs) / 60000), m.durationMin);
    text(buf, W - M - 144, BODY_Y + 27, F::S, C::ACCENT, td::middle_right);
  }

  // Timeline.
  constexpr int ty = BODY_Y + 56, th = 200;
  card(M, ty, W - 2 * M, th);
  if (!m.running && !m.count) {
    para("Monitor pings your router and the internet once a second for the time you choose, and records each "
         "minute: average and worst ping, lost pings, lag spikes over 200 ms and Wi-Fi drop-outs. Start it where "
         "and when the problems happen (e.g. evening gaming), then check the timeline: problems on the router line "
         "point at the Wi-Fi, problems only on the internet line point at the broadband.",
         M + 20, ty + 24, W - 2 * M - 40, F::S, C::DIM, 26);
  } else {
    timeline(M, ty, W - 2 * M, th);
  }

  // Summary.
  constexpr int sy = ty + th + M, sh = BOTTOM - sy;
  card(M, sy, W - 2 * M, sh);
  if (!m.running && !m.count) {
    para("Each minute is saved to the SD card (/logs/net-YYYY-MM-DD.csv) and shown at http://" HOSTNAME
         ".local/logs, where you can also start a run from your phone.",
         M + 20, sy + 16, W - 2 * M - 40);
    return;
  }
  const char *v = m.verdict();
  const uint16_t vc = v[0] == 'H' ? C::GREEN : v[0] == 'I' ? C::YELLOW : v[0] == 'N' ? C::DIM : C::RED;
  chip(M + 14, sy + 12, v, vc, C::BG, F::S);
  auto pct = [](uint32_t lost, uint32_t sent) { return sent ? 100.0f * lost / sent : 0.0f; };
  uint32_t rSum = 0, iSum = 0;
  for (int i = 0; i < m.count; ++i) {
    rSum += m.log[i].routerAvg;
    iSum += m.log[i].inetAvg;
  }
  const int n = max<int>(m.count, 1);
  const int c1 = M + 210, c2 = M + 470;
  const float rl = pct(m.routerLost, m.routerSent), il = pct(m.inetLost, m.inetSent);
  textf(c1, sy + 12, F::XS, rl > 1 ? C::RED : C::TEXT, td::top_left, "Router: %lu ms avg, %.1f%% lost",
        static_cast<unsigned long>(rSum / n), rl);
  textf(c2, sy + 12, F::XS, il > 1 ? C::RED : C::TEXT, td::top_left, "Internet: %lu ms avg, %.1f%% lost",
        static_cast<unsigned long>(iSum / n), il);
  textf(c1, sy + 32, F::XS, m.spikes ? C::ORANGE : C::TEXT, td::top_left, "Lag spikes >%u ms: %lu", MONITOR_SPIKE_MS,
        static_cast<unsigned long>(m.spikes));
  textf(c2, sy + 32, F::XS, m.dropouts ? C::RED : C::TEXT, td::top_left, "Wi-Fi drop-outs: %lu",
        static_cast<unsigned long>(m.dropouts));
  if (m.worstMs) {
    char hm[6];
    hhmm(m.worstAt, hm);
    textf(c1, sy + 52, F::XS, C::DIM, td::top_left, "Worst ping %u ms at %s", m.worstMs, hm);
  }
  textf(c2, sy + 52, F::XS, C::DIM, td::top_left, "Weakest signal %d dBm", m.rssiMin);
  if (!run) {
    char a[16];
    ago(m.endedAtMs, a, sizeof(a));
    textf(M + 14, sy + 56, F::XS, C::DIM, td::top_left, "Ended %s", a);
  }
  text(Log::sdReady() ? "Saved each minute to SD: /logs/net-*.csv" : "No SD card: results kept until restart", M + 14,
       BOTTOM - 22, F::XS, C::DIM);
}

// ================================================================ Help

/** @brief Term in accent colour followed by its explanation; returns the next y. */
int term(const char *name, const char *desc, int x, int y, int w) {
  text(name, x, y, F::S, C::ACCENT);
  return para(desc, x, y + 22, w) + 8;
}

}  // namespace

void pageWifi() {
  if (!psram(snap)) return placeholder(0, BODY_Y, W, BODY_H, "Out of memory");
  WifiScan::copy(*snap);
  sortNets();
  listCard();
  connectionCard();
  signalGraph();
}

void pageNetTest() {
  testExplain();
  testResults();
}

void pageNetMonitor() {
  if (!psram(mon)) return placeholder(0, BODY_Y, W, BODY_H, "Out of memory");
  NetTest::monitor(*mon);
  monitorPage();
}

void pageNetHelp() {
  card(LX, LY, LW, LH, "What the numbers mean");
  int y = LY + 40;
  const int w = LW - 28, x = LX + 14;
  y = term("Signal (dBm)", "Strength of the router's radio here. -50 excellent, -60 strong, -70 OK, below -80 unreliable.",
           x, y, w);
  y = term("Ping / latency (ms)", "Time for a message to go and come back. Games want under 30 ms to the internet.", x, y,
           w);
  y = term("Jitter (ms)", "How much ping varies. High jitter = stutter and rubber-banding in games and calls.", x, y, w);
  term("Loss, spikes, drop-outs", "Lost pings, pings over 200 ms, and times Wi-Fi disconnected: the 'glitches'.", x, y,
       w);

  card(RX, LY, RW, LH, "How to track down a problem");
  y = LY + 40;
  const int x2 = RX + 14, w2 = RW - 28;
  y = term("1. Signal", "Carry the panel (USB power bank) to the room and check the live dBm on the Signal tab.", x2, y,
           w2);
  y = term("2. Test", "Run a 30 s test there. Router bad = Wi-Fi problem; only internet bad = broadband problem.", x2, y,
           w2);
  y = term("3. Monitor", "For problems that come and go, leave a 30-120 min Monitor running while they happen.", x2, y, w2);
  term("Logs", "Results are saved to the SD card and shown at http://" HOSTNAME ".local/logs (start runs there too).", x2,
       y, w2);
}

bool tapWifi(int x, int y) {
  switch (buttonAt(x, y)) {
    case BTN_UP: return dragWifi(ROWS * ROW_H);
    case BTN_DOWN: return dragWifi(-ROWS * ROW_H);
    default: break;
  }
  if (!snap || x >= LX + LW || y < ROW_TOP) return false;
  const int r = (y - ROW_TOP) / ROW_H;
  if (r >= rowCount) return false;
  const WifiNet &n = snap->nets[rowNet[r]];
  if (selected(n)) memset(state.wifiBssid, 0, 6);
  else memcpy(state.wifiBssid, n.bssid, 6);
  return true;
}

bool dragWifi(int dy) {
  int rows = -dy / ROW_H;
  if (rows == 0) rows = dy < 0 ? 1 : -1;
  state.wifiScroll = constrain(state.wifiScroll + rows, 0, max(0, listCount - ROWS));
  return true;
}

bool tapNetTest(int x, int y) {
  if (buttonAt(x, y) != BTN_RUN) return false;
  NetTest::start();
  return true;
}

bool tapNetMonitor(int x, int y) {
  const int b = buttonAt(x, y);
  if (b == BTN_MON_START) {
    if (NetTest::monitoring()) NetTest::stopMonitor();
    else NetTest::startMonitor(state.monitorMins);
    return true;
  }
  if (b >= BTN_MIN_BASE && b < BTN_MIN_BASE + 4) {
    state.monitorMins = MONITOR_CHOICES[b - BTN_MIN_BASE];
    return true;
  }
  return false;
}

}  // namespace UiDraw
