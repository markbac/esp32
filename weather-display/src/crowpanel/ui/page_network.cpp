/**
 * @file    page_network.cpp
 * @brief   Network section: Signal, Test, Monitor and Help pages.
 *
 * @details
 *  - **Signal**: live signal of the joined access point (updated every
 *    second, so the panel works as a walk-around meter on a USB power bank),
 *    every network in range from a scan each minute, and a 3-hour signal
 *    history graph. Tap a network to highlight it on the graph.
 *  - **Crowding**: 2.4 GHz channel map from the scans (your channel, overlapping
 *    networks, the quietest of 1/6/11) and a 20-second air survey (AirSurvey):
 *    devices talking to your router, other devices on the channel, air time
 *    busy, re-sent frames and the busiest devices.
 *  - **Test**: a 30-second check (NetTest::start): 20 pings to the router,
 *    20 to the internet, a DNS lookup and an 8-second download, with a
 *    plain-English diagnosis.
 *  - **Monitor**: watches the connection for 15 minutes to 8 hours
 *    (NetTest::startMonitor): pings every second, signal, DNS each minute and
 *    an air survey; views for Ping, Signal, Air (devices / busy / re-sent)
 *    and Events (drop-outs with reasons, AP moves, spikes, losses).
 *  - **Help**: what each figure means and how to use the tests.
 *
 *  Results also go to the SD card (/logs/net-YYYY-MM-DD.csv) and the web
 *  page /logs, where tests can be started from a phone.
 */
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <initializer_list>
#include <new>
#include <utility>

#include "air_survey.h"
#include "connectivity.h"
#include "log.h"
#include "net_test.h"
#include "ui_draw.h"
#include "wifi_scan.h"

namespace UiDraw {
namespace {

enum Btn : int { BTN_RUN = 1, BTN_UP, BTN_DOWN, BTN_MON_START, BTN_LISTEN, BTN_MIN_BASE = 10, BTN_VIEW_BASE = 20 };


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
  char line[200] = "";
  const char *p = s;
  while (*p) {
    const char *sp = strchr(p, ' ');
    const size_t wl = sp ? static_cast<size_t>(sp - p) : strlen(p);
    char trial[400];
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

// ================================================================ Crowding

uint8_t family[WIFI_MAX_FAMILY][2];  ///< Your Wi-Fi system's address prefixes (router + mesh nodes).
uint8_t familyN = 0;

/** @brief How much other people's networks would interfere on channel @p c (0 = none). */
float channelLoad(const WifiScanData &d, int c) {
  float load = 0;
  for (uint8_t i = 0; i < d.count; ++i) {
    const WifiNet &n = d.nets[i];
    if (n.rssi == WIFI_NOT_SEEN || inFamily(n.bssid, family, familyN)) continue;
    const int dist = abs(n.channel - c);
    if (dist > 4) continue;
    load += (1.0f - dist / 5.0f) * constrain((n.rssi + 100) / 50.0f, 0.0f, 1.0f);
  }
  return load;
}

void channelCard() {
  const WifiScanData &d = *snap;
  card(LX, LY, LW, LH, "Channels (2.4 GHz)");
  const int myCh = Net::connected() ? WiFi.channel() : 0;
  familyN = WifiScan::ownFamily(family);
  // One column per channel; a block per network, strongest at the bottom.
  const int gx = LX + 20, gy = LY + 40, gw = LW - 40, gh = 150, cw = gw / 13;
  canvas.drawFastHLine(gx, gy + gh, gw, C::LINE);
  for (int c = 1; c <= 13; ++c) {
    const int x = gx + (c - 1) * cw;
    if (c == myCh) canvas.drawRoundRect(x, gy - 4, cw - 2, gh + 24, 4, C::ACCENT);
    textf(x + cw / 2 - 1, gy + gh + 4, F::XS, c == myCh ? C::ACCENT : (c == 1 || c == 6 || c == 11 ? C::TEXT : C::DIM),
          td::top_center, "%d", c);
    int8_t rs[WIFI_MAX_NETS];
    int k = 0;
    for (uint8_t i = 0; i < d.count; ++i) {
      if (d.nets[i].channel == c && d.nets[i].rssi != WIFI_NOT_SEEN) rs[k++] = d.nets[i].rssi;
    }
    std::sort(rs, rs + k, [](int8_t a, int8_t b) { return a > b; });
    for (int j = 0; j < k && j < 13; ++j) canvas.fillRect(x + 3, gy + gh - (j + 1) * 11, cw - 8, 9, rssiColor(rs[j]));
    if (k > 13) text("+", x + cw / 2, gy - 2, F::XS, C::TEXT, td::bottom_center);
  }
  int y = gy + gh + 26;
  if (!d.scans) {
    text("Waiting for the first scan...", LX + 14, y, F::XS, C::DIM);
    return;
  }
  if (myCh) {
    int ownAps = 0, ownOnCh = 0, overlap = 0, strong = 0, units = 0;
    uint64_t seen[WIFI_MAX_NETS];
    for (uint8_t i = 0; i < d.count; ++i) {
      const WifiNet &n = d.nets[i];
      if (n.rssi == WIFI_NOT_SEEN) continue;
      if (inFamily(n.bssid, family, familyN)) {
        ++ownAps;
        if (n.channel == myCh) ++ownOnCh;
        const uint64_t k = radioKey(n.bssid);
        bool dup = false;
        for (int u = 0; u < units && !dup; ++u) dup = seen[u] == k;
        if (!dup) seen[units++] = k;
      } else if (abs(n.channel - myCh) <= 4) {
        ++overlap;
        if (n.rssi >= -70) ++strong;
      }
    }
    textf(LX + 14, y, F::XS, C::TEXT, td::top_left, "You: channel %d, %d mesh unit%s heard", myCh, units,
          units == 1 ? "" : "s");
    textf(LX + 14, y + 18, F::XS, ownAps >= 12 ? C::ORANGE : C::DIM, td::top_left,
          "%d network broadcasts (SSIDs x units), %d on ch %d", ownAps, ownOnCh, myCh);
    textf(LX + 14, y + 36, F::XS, strong >= 3 ? C::ORANGE : C::TEXT, td::top_left, "Neighbours overlapping: %d (%d strong)",
          overlap, strong);
    int best = 1;
    for (int c : {6, 11}) {
      if (channelLoad(d, c) < channelLoad(d, best)) best = c;
    }
    if (best != myCh && channelLoad(d, best) + 0.5f < channelLoad(d, myCh))
      textf(LX + 14, y + 54, F::XS, C::YELLOW, td::top_left, "Quieter: channel %d (router settings)", best);
    else
      text("Your channel is the quietest of 1/6/11", LX + 14, y + 54, F::XS, C::GREEN);
  }
  para("Blocks = networks, colour = signal. 1, 6 and 11 don't overlap.", LX + 14, LY + LH - 42, LW - 28, F::XS, C::DIM);
}

void airCard() {
  card(RX, LY, RW, LH, "Who's using the air");
  const bool monRun = NetTest::monitoring();
  const bool listening = AirSurvey::active();
  button(BTN_LISTEN, RX + RW - 136, LY + 6, 124, 36, listening && !monRun ? "Listening" : "Listen 20 s",
         listening && !monRun);
  int y = LY + 52;
  if (monRun) {
    para("A Monitor is running and already listening: see its Air view for minute-by-minute figures.", RX + 14, y, RW - 28,
         F::S, C::ORANGE, 24);
    return;
  }
  if (listening) {
    canvas.fillRoundRect(RX + 14, y, RW - 28, 12, 6, C::CARD2);
    canvas.fillRoundRect(RX + 14, y, max(12, (RW - 28) * AirSurvey::progress() / 100), 12, 6, C::ACCENT);
    para("Listening to every frame on your router's channel. Only addresses, sizes and speeds are counted; "
         "the content is encrypted and never read.",
         RX + 14, y + 24, RW - 28);
    return;
  }
  const AirStats a = AirSurvey::lastTimed();
  if (!a.durationMs) {
    para("Listens to your router's channel for 20 seconds and counts the devices actually sending data, the other "
         "devices sharing the channel, how busy the air is and how often frames have to be re-sent. Too many busy "
         "devices or neighbours on one channel is a common cause of lag.",
         RX + 14, y, RW - 28, F::XS, C::MUTED, 18);
    para("This radio is 2.4 GHz only: devices on 5 GHz are not seen.", RX + 14, y + 112, RW - 28, F::XS, C::DIM);
    return;
  }
  auto big = [&](int x, const char *label, const char *value, uint16_t col) {
    text(value, x, y, F::M, col);
    text(label, x, y + 28, F::XS, C::MUTED);
  };
  char v[16];
  snprintf(v, sizeof(v), "%u", a.devicesOurs);
  big(RX + 14, "on your Wi-Fi", v, C::ACCENT);
  snprintf(v, sizeof(v), "%u", a.devicesOther);
  big(RX + 116, "neighbours", v, a.devicesOther > 2 * max<int>(a.devicesOurs, 3) ? C::ORANGE : C::TEXT);
  snprintf(v, sizeof(v), "%.0f%%", a.busyPct());
  big(RX + 212, "air busy", v, a.busyPct() >= 60 ? C::RED : a.busyPct() >= 35 ? C::ORANGE : C::GREEN);
  snprintf(v, sizeof(v), "%.0f%%", a.retryPct());
  big(RX + 300, "re-sent", v, a.retryPct() >= 25 ? C::RED : a.retryPct() >= 12 ? C::ORANGE : C::GREEN);
  y += 50;
  textf(RX + 14, y, F::XS, a.ourBeaconPct() >= 15 ? C::ORANGE : C::MUTED, td::top_left,
        "Beacons use %.0f%% of the air (%.0f%% are yours)", a.beaconPct(), a.ourBeaconPct());
  y += 22;
  text("Busiest devices on your Wi-Fi (share of data)", RX + 14, y, F::XS, C::MUTED);
  y += 20;
  for (int i = 0; i < a.topCount; ++i) {
    const AirTalker &t = a.top[i];
    const float share = a.talkerBytes ? 100.0f * t.bytes / a.talkerBytes : 0;
    textf(RX + 14, y, F::XS, C::TEXT, td::top_left, "..%02x:%02x:%02x%s", t.mac[3], t.mac[4], t.mac[5],
          (t.mac[0] & 2) ? " (private)" : "");
    const int bx = RX + 170, bw = RW - 170 - 60;
    canvas.fillRoundRect(bx, y + 3, bw, 10, 5, C::CARD2);
    canvas.fillRoundRect(bx, y + 3, max(6, static_cast<int>(bw * share / 100)), 10, 5, i ? C::ACCENT : C::ORANGE);
    textf(RX + RW - 14, y, F::XS, C::TEXT, td::top_right, "%.0f%%", share);
    y += 18;
  }
  if (!a.topCount) text("No data traffic heard", RX + 14, y, F::XS, C::DIM);
  const char *dx = a.ourBeaconPct() >= 15
                       ? "Your own access points' beacons use a lot of air: turn off unused SSIDs (guest, IoT) or "
                         "spread mesh nodes over channels."
                   : a.busyPct() >= 60  ? "The channel is congested: everything has to wait its turn, which feels like lag."
                   : a.retryPct() >= 25 ? "Many frames are re-sent: interference or devices with a weak signal."
                   : a.topCount && a.talkerBytes && a.top[0].bytes > a.talkerBytes * 0.7f && a.busyPct() >= 25
                       ? "One device is using most of the airtime (a big download or stream?)."
                   : a.devicesOther > 2 * max<int>(a.devicesOurs, 3) ? "Neighbours share this channel heavily: see the quieter channel on the left."
                                                                     : "The air looks fine at the moment.";
  para(dx, RX + 14, LY + LH - 50, RW - 28, F::XS, C::TEXT);
  textf(RX + 14, LY + LH - 20, F::XS, C::DIM, td::top_left, "Ch %u, %u s, %u APs heard. 2.4 GHz only.", a.channel,
        static_cast<unsigned>(a.durationMs / 1000), a.accessPoints);
}

// ================================================================ Monitor

const uint16_t MONITOR_CHOICES[] = {15, 30, 60, 120, 240, 480};  ///< Monitor lengths offered, minutes.
const char *const VIEW_NAMES[] = {"Ping", "Signal", "Air", "Events"};
constexpr int EVENT_ROWS = 8;

/** @brief Graph box shared by the timeline views; returns x for minute @p i. */
struct Plot {
  int gx, gy, gw, gh, slots, bw;
  int x(int i) const { return gx + i * gw / slots; }
};

Plot plotBox(int x, int y, int w, int h) {
  Plot p;
  p.gx = x + 44;
  p.gy = y + 40;
  p.gw = w - 88;
  p.gh = h - 66;
  p.slots = max<int>(mon->durationMin, 1);
  p.bw = max(1, p.gw / p.slots - 1);
  return p;
}

void axisTimes(const Plot &p) {
  const MonitorData &m = *mon;
  char hm[6];
  if (m.count) hhmm(m.log[0].at, hm);
  text(m.count ? hm : "0", p.gx, p.gy + p.gh + 4, F::XS, C::DIM);
  if (m.durationMin >= 60) textf(p.gx + p.gw, p.gy + p.gh + 4, F::XS, C::DIM, td::top_right, "+%u h", m.durationMin / 60);
  else textf(p.gx + p.gw, p.gy + p.gh + 4, F::XS, C::DIM, td::top_right, "+%u min", m.durationMin);
  if (m.running) canvas.drawFastVLine(p.x(min<int>(m.count, p.slots)), p.gy, p.gh, C::DIM);
}

void legend(int x, int y, std::initializer_list<std::pair<uint16_t, const char *>> items) {
  for (auto &k : items) {
    canvas.fillRect(x, y + 4, 8, 6, k.first);
    x += 12 + text(k.second, x + 12, y, F::XS, C::DIM) + 12;
  }
}

void viewPing(int x, int y, int w, int h) {
  const MonitorData &m = *mon;
  const Plot p = plotBox(x, y, w, h);
  constexpr int TOP_MS = 300;
  auto yFor = [&](int ms) { return p.gy + p.gh - min(ms, TOP_MS) * p.gh / TOP_MS; };
  for (int ms = 0; ms <= TOP_MS; ms += 100) {
    canvas.drawFastHLine(p.gx, yFor(ms), p.gw, C::CARD2);
    textf(p.gx - 6, yFor(ms), F::XS, C::DIM, td::middle_right, "%d", ms);
  }
  int prx = -1, pry = 0, pix = -1, piy = 0;
  for (int i = 0; i < m.count; ++i) {
    const MonitorMinute &e = m.log[i];
    const int xx = p.x(i), cx = xx + p.bw / 2;
    if (e.dropouts) canvas.fillRect(xx, p.gy, p.bw, p.gh, rgb(90, 30, 30));
    const int top = yFor(max(e.routerMax, e.inetMax));
    canvas.fillRect(xx, top, p.bw, p.gy + p.gh - top, e.spikes ? rgb(110, 70, 30) : C::CARD2);
    if (e.routerLost || e.inetLost) canvas.fillRect(xx, p.gy - 5, p.bw, 4, C::RED);
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
  axisTimes(p);
  text("ms", p.gx - 6, p.gy - 18, F::XS, C::DIM, td::top_right);
  legend(p.gx + 60, p.gy + p.gh + 4,
         {{C::ACCENT, "router avg"}, {C::PURPLE, "internet avg"}, {rgb(110, 70, 30), "worst >200"}, {C::RED, "lost/drop"}});
}

void viewSignal(int x, int y, int w, int h) {
  const MonitorData &m = *mon;
  const Plot p = plotBox(x, y, w, h);
  constexpr int TOP = -35, BOT = -95;
  auto yFor = [&](int db) { return p.gy + constrain((TOP - db) * p.gh / (TOP - BOT), 0, p.gh); };
  for (int db = -40; db >= -90; db -= 10) {
    canvas.drawFastHLine(p.gx, yFor(db), p.gw, db == -80 ? rgb(70, 40, 40) : C::CARD2);
    textf(p.gx - 6, yFor(db), F::XS, C::DIM, td::middle_right, "%d", db);
  }
  int px = -1, py = 0;
  for (int i = 0; i < m.count; ++i) {
    const MonitorMinute &e = m.log[i];
    const int xx = p.x(i), cx = xx + p.bw / 2;
    if (e.dropouts) canvas.fillRect(xx, p.gy, p.bw, p.gh, rgb(90, 30, 30));
    if (e.roams) canvas.drawFastVLine(cx, p.gy, p.gh, C::CYAN);
    if (e.rssiMin) canvas.fillRect(xx, yFor(e.rssiMin), p.bw, 2, rssiColor(e.rssiMin));
    if (e.rssiAvg) {
      const int yy = yFor(e.rssiAvg);
      if (px >= 0) canvas.drawWideLine(px, py, cx, yy, 1.5f, C::GREEN);
      px = cx;
      py = yy;
    }
  }
  axisTimes(p);
  text("dBm", p.gx - 6, p.gy - 18, F::XS, C::DIM, td::top_right);
  legend(p.gx + 60, p.gy + p.gh + 4, {{C::GREEN, "average"}, {C::ORANGE, "weakest"}, {C::CYAN, "moved AP"}, {C::RED, "drop-out"}});
}

void viewAir(int x, int y, int w, int h) {
  const MonitorData &m = *mon;
  if (!m.airSurvey) {
    placeholder(x, y + 20, w, h - 20, m.running ? "First minute of air figures soon..." : "No air figures in this run");
    return;
  }
  const Plot p = plotBox(x, y, w, h);
  const int devTop = max<int>(10, ((m.devicesMax + 4) / 5) * 5);
  auto yDev = [&](int n) { return p.gy + p.gh - min(n, devTop) * p.gh / devTop; };
  auto yPct = [&](int pct) { return p.gy + p.gh - min(pct, 100) * p.gh / 100; };
  for (int k = 0; k <= 4; ++k) {
    const int yy = p.gy + p.gh - k * p.gh / 4;
    canvas.drawFastHLine(p.gx, yy, p.gw, C::CARD2);
    textf(p.gx - 6, yy, F::XS, C::ACCENT, td::middle_right, "%d", devTop * k / 4);
    textf(p.gx + p.gw + 6, yy, F::XS, C::ORANGE, td::middle_left, "%d%%", 25 * k);
  }
  int bx = -1, by = 0, rx = -1, ry = 0;
  for (int i = 0; i < m.count; ++i) {
    const MonitorMinute &e = m.log[i];
    const int xx = p.x(i), cx = xx + p.bw / 2;
    canvas.fillRect(xx, yDev(e.devices), p.bw, p.gy + p.gh - yDev(e.devices), rgb(30, 60, 100));
    const int yb = yPct(e.busyPct), yr = yPct(e.retryPct);
    if (bx >= 0) canvas.drawWideLine(bx, by, cx, yb, 1.5f, C::ORANGE);
    if (rx >= 0) canvas.drawWideLine(rx, ry, cx, yr, 1.0f, C::RED);
    bx = rx = cx;
    by = yb;
    ry = yr;
  }
  axisTimes(p);
  text("devices", p.gx - 6, p.gy - 18, F::XS, C::ACCENT, td::top_right);
  legend(p.gx + 60, p.gy + p.gh + 4, {{rgb(30, 60, 100), "devices on router"}, {C::ORANGE, "air busy %"}, {C::RED, "re-sent %"}});
}

void viewEvents(int x, int y, int w, int h) {
  const MonitorData &m = *mon;
  const int n = m.eventCount;
  state.monitorEventScroll = constrain(state.monitorEventScroll, 0, max(0, n - EVENT_ROWS));
  int yy = y + 42;
  for (int r = 0; r < EVENT_ROWS; ++r) {
    const int i = n - 1 - state.monitorEventScroll - r;  // Newest first.
    if (i < 0) break;
    const MonitorEvent &e = m.events[i];
    char d[80];
    NetTest::describe(e, d, sizeof(d));
    const bool bad = e.type == MonitorEventType::Drop || e.type == MonitorEventType::Loss || e.type == MonitorEventType::DnsFail;
    const bool warn = e.type == MonitorEventType::Spike || e.type == MonitorEventType::Weak ||
                      e.type == MonitorEventType::Busy || e.type == MonitorEventType::Retries || e.type == MonitorEventType::Roam;
    canvas.fillCircle(x + 22, yy + 7, 4, bad ? C::RED : warn ? C::ORANGE : C::DIM);
    text(d, x + 36, yy, F::XS, bad ? C::TEXT : C::MUTED);
    yy += 19;
  }
  if (!n) placeholder(x, y + 20, w, h - 20, "No events yet");
  else if (n > EVENT_ROWS)
    textf(x + w - 14, y + h - 18, F::XS, C::DIM, td::top_right, "%d events, newest first - drag to scroll", n);
}

void monitorPage() {
  const MonitorData &m = *mon;
  const bool run = m.running;
  // Controls: duration, start/stop, status.
  for (int i = 0; i < 6; ++i) {
    char l[8];
    if (MONITOR_CHOICES[i] < 60) snprintf(l, sizeof(l), "%um", MONITOR_CHOICES[i]);
    else snprintf(l, sizeof(l), "%uh", MONITOR_CHOICES[i] / 60);
    button(BTN_MIN_BASE + i, M + i * 58, BODY_Y + 8, 52, 36, l, state.monitorMins == MONITOR_CHOICES[i], F::XS);
  }
  button(BTN_MON_START, W - M - 120, BODY_Y + 8, 120, 36, run ? "Stop" : "Start", !run);
  if (run) {
    const uint32_t mins = (millis() - m.startedAtMs) / 60000;
    textf(W - M - 132, BODY_Y + 26, F::S, C::ACCENT, td::middle_right, "%lu of %u min", static_cast<unsigned long>(mins),
          m.durationMin);
  }

  constexpr int ty = BODY_Y + 52, th = 216;
  card(M, ty, W - 2 * M, th);
  for (int i = 0; i < 4; ++i)
    button(BTN_VIEW_BASE + i, W - M - 8 - (4 - i) * 74, ty + 6, 68, 28, VIEW_NAMES[i], state.monitorView == i, F::XS);
  if (!run && !m.count && !m.eventCount) {
    para("Monitor pings your router and the internet every second for the time you choose and records each minute: "
         "ping times, lost pings, lag spikes, Wi-Fi drop-outs and their reason, moves between access points, signal, "
         "DNS, and (by listening to the channel) how many devices are busy and how crowded the air is. Start it where and "
         "when the problems happen.",
         M + 16, ty + 44, W - 2 * M - 32, F::XS, C::MUTED, 19);
  } else {
    static const char *const titles[] = {"Ping per minute", "Signal per minute", "Devices and air time per minute",
                                         "Events"};
    text(titles[state.monitorView], M + 14, ty + 12, F::S, C::MUTED);
    switch (state.monitorView) {
      case 0: viewPing(M, ty, W - 2 * M, th); break;
      case 1: viewSignal(M, ty, W - 2 * M, th); break;
      case 2: viewAir(M, ty, W - 2 * M, th); break;
      default: viewEvents(M, ty, W - 2 * M, th); break;
    }
  }

  // Summary.
  constexpr int sy = ty + th + M, sh = BOTTOM - sy;
  card(M, sy, W - 2 * M, sh);
  if (!run && !m.count) {
    para("Each minute and every event is saved to the SD card (/logs/net-YYYY-MM-DD.csv) and shown at http://" HOSTNAME
         ".local/logs, where you can also start a run from your phone. Up to 8 hours, e.g. a whole evening.",
         M + 16, sy + 12, W - 2 * M - 32);
    return;
  }
  const char *v = m.verdict();
  const uint16_t vc = v[0] == 'H' ? C::GREEN : v[0] == 'I' ? C::YELLOW : v[0] == 'N' ? C::DIM : C::RED;
  chip(M + 12, sy + 10, v, vc, C::BG, F::XS);
  if (!run) {
    char a[16];
    ago(m.endedAtMs, a, sizeof(a));
    textf(M + 12, sy + 38, F::XS, C::DIM, td::top_left, "ended %s", a);
  }
  auto pct = [](uint32_t lost, uint32_t sent) { return sent ? 100.0f * lost / sent : 0.0f; };
  uint32_t rSum = 0, iSum = 0;
  for (int i = 0; i < m.count; ++i) {
    rSum += m.log[i].routerAvg;
    iSum += m.log[i].inetAvg;
  }
  const int n = max<int>(m.count, 1);
  const float rl = pct(m.routerLost, m.routerSent), il = pct(m.inetLost, m.inetSent);
  const int c1 = M + 160, c2 = M + 370, c3 = M + 580, r0 = sy + 10, rs = 20;
  char hm[6] = "--:--";
  if (m.worstAt) hhmm(m.worstAt, hm);
  textf(c1, r0, F::XS, rl > 1 ? C::RED : C::TEXT, td::top_left, "Router %lu ms, %.1f%% lost",
        static_cast<unsigned long>(rSum / n), rl);
  textf(c1, r0 + rs, F::XS, il > 1 ? C::RED : C::TEXT, td::top_left, "Internet %lu ms, %.1f%% lost",
        static_cast<unsigned long>(iSum / n), il);
  textf(c1, r0 + 2 * rs, F::XS, C::DIM, td::top_left, "Worst %u ms at %s", m.worstMs, hm);
  textf(c2, r0, F::XS, m.spikes ? C::ORANGE : C::TEXT, td::top_left, "Spikes >%u ms: %lu", MONITOR_SPIKE_MS,
        static_cast<unsigned long>(m.spikes));
  textf(c2, r0 + rs, F::XS, m.dropouts ? C::RED : C::TEXT, td::top_left, "Drop-outs %lu, AP moves %lu",
        static_cast<unsigned long>(m.dropouts), static_cast<unsigned long>(m.roams));
  textf(c2, r0 + 2 * rs, F::XS, m.dnsFails ? C::ORANGE : C::DIM, td::top_left, "DNS failures %lu",
        static_cast<unsigned long>(m.dnsFails));
  textf(c3, r0, F::XS, C::TEXT, td::top_left, "Weakest signal %d dBm", m.rssiMin);
  if (m.airSurvey) {
    textf(c3, r0 + rs, F::XS, C::TEXT, td::top_left, "Up to %u devices busy", m.devicesMax);
    textf(c3, r0 + 2 * rs, F::XS, m.busyMax >= 60 ? C::ORANGE : C::DIM, td::top_left, "Air up to %u%% busy", m.busyMax);
  }
  text(Log::sdReady() ? "Saved to SD: /logs/net-*.csv" : "No SD card: kept until restart", M + 12, BOTTOM - 20, F::XS,
       C::DIM);
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

void pageNetCrowd() {
  if (!psram(snap)) return placeholder(0, BODY_Y, W, BODY_H, "Out of memory");
  WifiScan::copy(*snap);
  channelCard();
  airCard();
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
  y = term("2. Crowding", "Too many busy devices or neighbours on your channel? Listen 20 s; try the quieter channel.",
           x2, y, w2);
  y = term("3. Test", "30 s test. Router bad = Wi-Fi problem; only internet bad = broadband problem.", x2, y, w2);
  y = term("4. Monitor", "For problems that come and go: run 1-8 h while they happen, then check Events.", x2, y, w2);
  para("Logs: SD card and http://" HOSTNAME ".local/logs. This radio sees 2.4 GHz only, not 5 GHz.", x2, y, w2, F::XS,
       C::DIM);
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
  if (b >= BTN_MIN_BASE && b < BTN_MIN_BASE + 6) {
    state.monitorMins = MONITOR_CHOICES[b - BTN_MIN_BASE];
    return true;
  }
  if (b >= BTN_VIEW_BASE && b < BTN_VIEW_BASE + 4) {
    state.monitorView = static_cast<uint8_t>(b - BTN_VIEW_BASE);
    state.monitorEventScroll = 0;
    return true;
  }
  return false;
}

bool dragNetMonitor(int dy) {
  if (state.monitorView != 3) return false;
  int rows = dy / 19;  // Newest first: dragging down shows older events.
  if (rows == 0) rows = dy > 0 ? 1 : -1;
  state.monitorEventScroll = max(0, state.monitorEventScroll + rows);
  return true;
}

bool tapNetCrowd(int x, int y) {
  if (buttonAt(x, y) != BTN_LISTEN || NetTest::monitoring() || NetTest::running()) return false;
  AirSurvey::startTimed(20000);
  return true;
}

}  // namespace UiDraw
