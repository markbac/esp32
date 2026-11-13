/**
 * @file    page_settings.cpp
 * @brief   "Settings" page: auto page (and which pages), sleep mode and backlight, editable by touch.
 *          The flight radius is set on the Flights page.
 *
 * @details Changes apply at once and are written to flash a few seconds after
 *          the last tap (Settings::set / Settings::flush), so they survive a
 *          restart and show up on the web settings page too.
 *
 * @verbatim
 *   +-- Auto page ------------------+ +-- Sleep mode --------------------+
 *   | Auto page        [Off] [On]   | | Sleep mode        [Off] [On]     |
 *   | Seconds per page [-] 15 s [+] | | Sleep at        [-] 23:00 [+]    |
 *   | On: 15 s on each page         | | Wake at         [-] 07:00 [+]    |
 *   +-- Pages in auto page ---------+ +-- Backlight ---------------------+
 *   | [Now][Forecast][Radar][Alerts]| | Day / Night       [-] 200 [+]    |
 *   +-------------------------------+ +----------------------------------+
 * @endverbatim
 */
#include "board_config.h"
#include "config.h"
#include "connectivity.h"
#include "settings.h"
#include "ui_draw.h"

namespace UiDraw {
namespace {

enum Btn : int {
  AUTO_OFF = 1, AUTO_ON, SECS_DOWN, SECS_UP,
  SLEEP_OFF, SLEEP_ON, FROM_DOWN, FROM_UP, TO_DOWN, TO_UP,
  PAGE_BASE = 100,  ///< + page index: toggle that page in the auto page rotation.
  DAY_DOWN, DAY_UP, NIGHT_DOWN, NIGHT_UP,
};

constexpr int CW = (W - 3 * M) / 2;            ///< Card width.
constexpr int X0 = M, X1 = 2 * M + CW;
constexpr int CARD_TOP_Y = BODY_Y + M, CARD_TOP_H = 200;
constexpr int BOT_Y = CARD_TOP_Y + CARD_TOP_H + M, BOT_H = BODY_Y + BODY_H - M - BOT_Y;
constexpr int ROW = 48;                         ///< Row pitch.

/** @brief Label on the left, "[-] value [+]" on the right. */
void stepper(int x, int y, const char *label, const char *value, int down, int up) {
  text(label, x + 16, y + 18, F::S, C::TEXT, td::middle_left);
  const int r = x + CW - 16;
  button(up, r - 48, y, 48, 36, "+", false, F::M);
  text(value, r - 48 - 52, y + 19, F::S, C::ACCENT, td::middle_center);
  button(down, r - 48 - 104 - 48, y, 48, 36, "-", false, F::M);
}

/** @brief Label on the left, [Off] [On] on the right. */
void toggle(int x, int y, const char *label, bool on, int offId, int onId) {
  text(label, x + 16, y + 18, F::S, C::TEXT, td::middle_left);
  const int r = x + CW - 16;
  button(onId, r - 70, y, 70, 36, "On", on);
  button(offId, r - 70 - 8 - 70, y, 70, 36, "Off", !on);
}

void fmtTime(uint16_t m, char *buf, size_t n) { snprintf(buf, n, "%02d:%02d", m / 60, m % 60); }

/** @brief "in 3 h 10 min" until minute-of-day @p target. */
void until(int nowMin, int target, char *buf, size_t n) {
  const int d = (target - nowMin + 1440) % 1440;
  if (d >= 60) snprintf(buf, n, "in %d h %02d min", d / 60, d % 60);
  else snprintf(buf, n, "in %d min", d);
}

}  // namespace

void pageSettings() {
  const SettingsData s = Settings::get();
  char buf[64];

  // Auto page.
  card(X0, CARD_TOP_Y, CW, CARD_TOP_H, "Auto page");
  toggle(X0, CARD_TOP_Y + 40, "Auto page", s.autoPage, AUTO_OFF, AUTO_ON);
  snprintf(buf, sizeof(buf), "%u s", s.autoPageSecs);
  stepper(X0, CARD_TOP_Y + 40 + ROW, "Per page", buf, SECS_DOWN, SECS_UP);
  if (s.autoPage) {
    snprintf(buf, sizeof(buf), "ON: moves to the next page every %u s", s.autoPageSecs);
    text(buf, X0 + 16, CARD_TOP_Y + 40 + 2 * ROW + 6, F::XS, C::GREEN);
    textf(X0 + 16, CARD_TOP_Y + 40 + 2 * ROW + 24, F::XS, C::DIM, td::top_left,
          "Pages ticked below. Pauses %lu s after a touch.", static_cast<unsigned long>(CP_TOUCH_HOLD_MS / 1000));
  } else {
    text("OFF: pages change only when you tap or swipe", X0 + 16, CARD_TOP_Y + 40 + 2 * ROW + 6, F::XS, C::MUTED);
  }

  // Sleep mode.
  card(X1, CARD_TOP_Y, CW, CARD_TOP_H, "Sleep mode");
  toggle(X1, CARD_TOP_Y + 40, "Sleep mode", s.sleepEnabled, SLEEP_OFF, SLEEP_ON);
  char from[6], to[6];
  fmtTime(s.sleepStartMin, from, sizeof(from));
  fmtTime(s.sleepEndMin, to, sizeof(to));
  stepper(X1, CARD_TOP_Y + 40 + ROW, "Sleep at", from, FROM_DOWN, FROM_UP);
  stepper(X1, CARD_TOP_Y + 40 + 2 * ROW, "Wake at", to, TO_DOWN, TO_UP);
  tm t;
  if (!s.sleepEnabled) {
    text("OFF: the screen stays on all the time", X1 + 16, CARD_TOP_Y + CARD_TOP_H - 22, F::XS, C::MUTED);
  } else if (s.sleepStartMin == s.sleepEndMin) {
    text("Sleep and wake times are the same: never sleeps", X1 + 16, CARD_TOP_Y + CARD_TOP_H - 22, F::XS, C::ORANGE);
  } else if (Net::localTime(t)) {
    const int now = t.tm_hour * 60 + t.tm_min;
    char when[24];
    if (Settings::inSleepWindow(s, now)) {
      until(now, s.sleepEndMin, when, sizeof(when));
      snprintf(buf, sizeof(buf), "ON: %s-%s, sleeping (wakes %s)", from, to, when);
    } else {
      until(now, s.sleepStartMin, when, sizeof(when));
      snprintf(buf, sizeof(buf), "ON: screen off %s-%s, next %s", from, to, when);
    }
    text(buf, X1 + 16, CARD_TOP_Y + CARD_TOP_H - 22, F::XS, C::GREEN);
  } else {
    snprintf(buf, sizeof(buf), "ON: %s-%s (waiting for the clock)", from, to);
    text(buf, X1 + 16, CARD_TOP_Y + CARD_TOP_H - 22, F::XS, C::GREEN);
  }

  // Pages included in auto paging.
  card(X0, BOT_Y, CW, BOT_H, "Pages in auto page");
  {
    constexpr int cols = 5, bw = (CW - 28 - (cols - 1) * 6) / cols, bh = 30;
    int k = 0;
    for (int i = 0; i < static_cast<int>(Page::Count); ++i) {
      if (!Ui::rotatable(static_cast<Page>(i))) continue;
      const int bx = X0 + 14 + (k % cols) * (bw + 6), by = BOT_Y + 36 + (k / cols) * (bh + 6);
      const char *label = static_cast<Page>(i) == Page::Air ? "Air" : Ui::pageName(static_cast<Page>(i));  // Fit the chip.
      button(PAGE_BASE + i, bx, by, bw, bh, label, (s.autoPageMask >> i) & 1, F::XS);
      ++k;
    }
  }

  // Backlight.
  card(X1, BOT_Y, CW, BOT_H, "Backlight");
  snprintf(buf, sizeof(buf), "%u", s.backlightDay);
  stepper(X1, BOT_Y + 36, "Day", buf, DAY_DOWN, DAY_UP);
  if (s.backlightNight) snprintf(buf, sizeof(buf), "%u", s.backlightNight);
  else snprintf(buf, sizeof(buf), "off");
  stepper(X1, BOT_Y + 36 + ROW - 4, "Night", buf, NIGHT_DOWN, NIGHT_UP);
  text("Changes save automatically (also on the web", X1 + 16, BOT_Y + BOT_H - 40, F::XS, C::DIM);
  text("at http://" HOSTNAME ".local/settings)", X1 + 16, BOT_Y + BOT_H - 22, F::XS, C::DIM);
}

bool tapSettings(int x, int y) {
  const int id = buttonAt(x, y);
  if (id < 0) return false;
  SettingsData s = Settings::get();
  auto clampAdd = [](int v, int d, int lo, int hi) { return static_cast<uint8_t>(constrain(v + d, lo, hi)); };
  auto addMin = [](uint16_t m, int d) { return static_cast<uint16_t>((m + d + 1440) % 1440); };
  switch (id) {
    case AUTO_OFF: s.autoPage = false; break;
    case AUTO_ON: s.autoPage = true; break;
    case SECS_DOWN: s.autoPageSecs = Settings::step(Settings::PAGE_SECS, Settings::PAGE_SECS_COUNT, s.autoPageSecs, -1); break;
    case SECS_UP: s.autoPageSecs = Settings::step(Settings::PAGE_SECS, Settings::PAGE_SECS_COUNT, s.autoPageSecs, 1); break;
    case SLEEP_OFF: s.sleepEnabled = false; break;
    case SLEEP_ON: s.sleepEnabled = true; break;
    case FROM_DOWN: s.sleepStartMin = addMin(s.sleepStartMin, -15); break;
    case FROM_UP: s.sleepStartMin = addMin(s.sleepStartMin, 15); break;
    case TO_DOWN: s.sleepEndMin = addMin(s.sleepEndMin, -15); break;
    case TO_UP: s.sleepEndMin = addMin(s.sleepEndMin, 15); break;
    case DAY_DOWN: s.backlightDay = clampAdd(s.backlightDay, -25, 5, 255); break;
    case DAY_UP: s.backlightDay = clampAdd(s.backlightDay, 25, 5, 255); break;
    case NIGHT_DOWN: s.backlightNight = clampAdd(s.backlightNight, -10, 0, 255); break;
    case NIGHT_UP: s.backlightNight = clampAdd(s.backlightNight, 10, 0, 255); break;
    default:
      if (id >= PAGE_BASE && id < PAGE_BASE + static_cast<int>(Page::Count)) {
        s.autoPageMask ^= 1UL << (id - PAGE_BASE);
        break;
      }
      return false;
  }
  Settings::set(s);
  return true;
}

}  // namespace UiDraw
