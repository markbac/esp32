/**
 * @file    ui_draw.h
 * @brief   Internal to the CrowPanel UI: canvas, palette, fonts, layout and helpers.
 *
 * @details Screen map (800 x 480):
 * @verbatim
 *   y   0..43   status bar   [21:57 Sun 8 Nov]   [Page title]   [T][aurora][!][wifi]
 *   y  44..423  page body (800 x 380); cards inset 8 px
 *   y 424..479  tab bar: Now | Forecast | Radar | Air | Sky | Alerts | Flights | History | System
 * @endverbatim
 */
#pragma once

#include "display.h"
#include "ui.h"

#include "aurora.h"
#include "data_service.h"
#include "theme.h"
#include "warnings.h"

namespace UiDraw {

using Datum = lgfx::textdatum::textdatum_t;
namespace td = lgfx::textdatum;

/** @name Layout */
///@{
constexpr int W = Display::WIDTH;
constexpr int H = Display::HEIGHT;
constexpr int TOP_H = 44;           ///< Status bar height.
constexpr int TAB_H = 56;           ///< Tab bar height.
constexpr int BODY_Y = TOP_H;       ///< First body row.
constexpr int BODY_H = H - TOP_H - TAB_H;  ///< 380.
constexpr int TAB_Y = H - TAB_H;    ///< 424.
constexpr int M = 8;                ///< Card margin.
///@}

/** @brief Pack 8-bit channels into RGB565. */
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/** @brief CrowPanel palette (dark theme). */
namespace C {
constexpr uint16_t BG = rgb(10, 14, 22);       ///< Page background.
constexpr uint16_t BAR = rgb(18, 24, 36);      ///< Status and tab bars.
constexpr uint16_t CARD = rgb(24, 31, 44);     ///< Card background.
constexpr uint16_t CARD2 = rgb(34, 43, 60);    ///< Raised element / active tab.
constexpr uint16_t LINE = rgb(52, 62, 80);     ///< Dividers, axes.
constexpr uint16_t TEXT = rgb(235, 240, 245);  ///< Primary text.
constexpr uint16_t MUTED = rgb(139, 148, 160); ///< Labels.
constexpr uint16_t DIM = rgb(88, 98, 112);     ///< Tertiary text.
constexpr uint16_t ACCENT = rgb(88, 166, 255); ///< Selection / links.
constexpr uint16_t ORANGE = rgb(255, 166, 87);
constexpr uint16_t YELLOW = rgb(242, 204, 96);
constexpr uint16_t GREEN = rgb(63, 185, 80);
constexpr uint16_t CYAN = rgb(86, 212, 221);
constexpr uint16_t BLUE = rgb(88, 166, 255);
constexpr uint16_t RED = rgb(248, 81, 73);
constexpr uint16_t PURPLE = rgb(188, 140, 255);
}  // namespace C

/** @brief Font sizes (DejaVu bitmap fonts; nominal pixel height). */
enum class F : uint8_t { XS, S, M, L, XL, XXL, HUGE };

/** @brief Full-screen sprite every page draws into. */
extern LGFX_Sprite canvas;

/** @brief Interactive UI state shared between frame and pages. */
struct UiState {
  Page page = Page::Now;          ///< Visible page.
  bool radarPaused = false;       ///< Radar animation frozen.
  int radarFrame = -1;            ///< Frame shown while paused.
  uint8_t historyDays = 1;        ///< History page range: 1, 3 or 7 days.
  char selectedHex[7] = "";       ///< Aircraft selected on the Flights page.
};
extern UiState state;

/** @name Text */
///@{
void setFont(F f);
int fontHeight(F f);
int textWidth(const char *s, F f);
/** @brief Draw text; returns its width. */
int text(const char *s, int x, int y, F f, uint16_t color, Datum d = td::top_left);
/** @brief printf-style text; returns its width. */
int textf(int x, int y, F f, uint16_t color, Datum d, const char *fmt, ...) __attribute__((format(printf, 6, 7)));
/**
 * @brief Temperature with a drawn degree ring (fonts are ASCII-only).
 * @param d  Horizontal alignment from the datum (left/centre/right); y is the text top.
 * @return Total width.
 */
int temp(float t, int x, int y, F f, uint16_t color, Datum d = td::top_left, int decimals = 0);
///@}

/** @name Shapes */
///@{
/** @brief Rounded card with optional title in the top-left. */
void card(int x, int y, int w, int h, const char *title = nullptr);
/** @brief Rounded "pill" with text; returns its width. */
int chip(int x, int y, const char *label, uint16_t bg, uint16_t fg, F f = F::S);
/** @brief Placeholder message centred in a box. */
void placeholder(int x, int y, int w, int h, const char *msg);
/** @brief Wi-Fi bars icon (bars 0-4), 24 x 18. */
void wifiBars(int x, int y, int bars, bool connected);
/** @brief Filled warning triangle with "!" of the given size. */
void warningTriangle(int x, int y, int size, uint16_t color);
///@}

/** @name Colours and formatting */
///@{
uint16_t warningColor(WarningLevel l);
uint16_t auroraColor(AuroraLevel l);
uint16_t aqiColor(uint8_t bandIdx);
/** @brief Local "HH:MM" for a UTC epoch (buf >= 6). */
void hhmm(time_t t, char *buf);
/** @brief "12 s ago" / "5 min ago" / "3 h ago" for a millis() timestamp (buf >= 16). */
void ago(uint32_t atMs, char *buf, size_t n);
///@}

/** @name Page renderers and touch handlers */
///@{
void pageNow();
void pageForecast();
void pageRadar();
void pageAir();
void pageSky();
void pageAlerts();
void pageFlights();
void pageHistory();
void pageSystem();
bool tapRadar(int x, int y);
bool tapFlights(int x, int y);
bool tapHistory(int x, int y);
///@}

}  // namespace UiDraw
