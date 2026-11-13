/**
 * @file    settings.cpp
 * @brief   NVS-backed runtime settings with compiled-in defaults.
 */
#include "settings.h"
#include "log.h"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"
#include "secrets_include.h"

#ifndef CP_BACKLIGHT_DEFAULT
#define CP_BACKLIGHT_DEFAULT 200
#endif
#ifndef CP_BACKLIGHT_NIGHT_DEFAULT
#define CP_BACKLIGHT_NIGHT_DEFAULT 60
#endif

namespace {

SettingsData current;
SemaphoreHandle_t mutex = nullptr;
bool dirty = false;          ///< set() changes not yet written to flash.
uint32_t dirtyAtMs = 0;      ///< millis() of the last set().
const char *const NS = "wxcfg";

struct Lock {
  Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex); }
};

/** @brief Build-time defaults. */
SettingsData defaults() {
  SettingsData d;
  d.wifiSsid = "";  // Empty = compiled-in WIFI_SSID/WIFI_PASSWORD.
  d.haUrl = HA_URL;
#ifdef HA_TOKEN
  d.haToken = HA_TOKEN;
#endif
  d.nasHost = HOMELAB_NAS_HOST;
  d.glancesPort = GLANCES_PORT;
  d.speedtestPort = SPEEDTEST_PORT;
  d.kumaPort = KUMA_PORT;
  d.kumaSlug = KUMA_SLUG;
  for (const char *h : SONOS_HOSTS) {
    if (d.sonosHosts.length()) d.sonosHosts += ",";
    d.sonosHosts += h;
  }
  d.backlightDay = CP_BACKLIGHT_DEFAULT;
  d.backlightNight = CP_BACKLIGHT_NIGHT_DEFAULT;
  d.autoPage = false;
  d.autoPageSecs = 15;
  d.autoPageMask = 0xFFFFFFFFUL;  // Every page the board allows.
  d.sleepEnabled = false;
  d.sleepStartMin = 23 * 60;
  d.sleepEndMin = 7 * 60;
  d.flightRadiusNm = FLIGHT_RADIUS_NM;
  return d;
}

/** @brief Keep loaded values inside the ranges the UI offers. */
void sanitise(SettingsData &s) {
  if (s.autoPageSecs < 5 || s.autoPageSecs > 3600) s.autoPageSecs = 15;
  if (s.sleepStartMin >= 1440) s.sleepStartMin = 23 * 60;
  if (s.sleepEndMin >= 1440) s.sleepEndMin = 7 * 60;
  if (s.flightRadiusNm < 1 || s.flightRadiusNm > 250) s.flightRadiusNm = FLIGHT_RADIUS_NM;
}

/** @brief Write @p s to NVS. */
bool write(const SettingsData &s) {
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putString("wifiSsid", s.wifiSsid);
  p.putString("wifiPass", s.wifiPassword);
  p.putString("haUrl", s.haUrl);
  p.putString("haToken", s.haToken);
  p.putString("nasHost", s.nasHost);
  p.putUShort("glancesPort", s.glancesPort);
  p.putUShort("speedPort", s.speedtestPort);
  p.putUShort("kumaPort", s.kumaPort);
  p.putString("kumaSlug", s.kumaSlug);
  p.putString("sonos", s.sonosHosts);
  p.putUChar("blDay", s.backlightDay);
  p.putUChar("blNight", s.backlightNight);
  p.putBool("autoPage", s.autoPage);
  p.putUShort("pageSecs", s.autoPageSecs);
  p.putULong("pageMask", s.autoPageMask);
  p.putBool("sleepOn", s.sleepEnabled);
  p.putUShort("sleepFrom", s.sleepStartMin);
  p.putUShort("sleepTo", s.sleepEndMin);
  p.putUShort("fltRadius", s.flightRadiusNm);
  p.putString("admin", s.adminPassword);
  p.end();
  return true;
}

}  // namespace

namespace Settings {

void begin() {
  mutex = xSemaphoreCreateMutex();
  SettingsData s = defaults();
  Preferences p;
  if (p.begin(NS, true)) {
    s.wifiSsid = p.getString("wifiSsid", s.wifiSsid);
    s.wifiPassword = p.getString("wifiPass", s.wifiPassword);
    s.haUrl = p.getString("haUrl", s.haUrl);
    s.haToken = p.getString("haToken", s.haToken);
    s.nasHost = p.getString("nasHost", s.nasHost);
    s.glancesPort = p.getUShort("glancesPort", s.glancesPort);
    s.speedtestPort = p.getUShort("speedPort", s.speedtestPort);
    s.kumaPort = p.getUShort("kumaPort", s.kumaPort);
    s.kumaSlug = p.getString("kumaSlug", s.kumaSlug);
    s.sonosHosts = p.getString("sonos", s.sonosHosts);
    s.backlightDay = p.getUChar("blDay", s.backlightDay);
    s.backlightNight = p.getUChar("blNight", s.backlightNight);
    s.autoPage = p.getBool("autoPage", s.autoPage);
    s.autoPageSecs = p.getUShort("pageSecs", s.autoPageSecs);
    s.autoPageMask = p.getULong("pageMask", s.autoPageMask);
    s.sleepEnabled = p.getBool("sleepOn", s.sleepEnabled);
    s.sleepStartMin = p.getUShort("sleepFrom", s.sleepStartMin);
    s.sleepEndMin = p.getUShort("sleepTo", s.sleepEndMin);
    s.flightRadiusNm = p.getUShort("fltRadius", s.flightRadiusNm);
    s.adminPassword = p.getString("admin", s.adminPassword);
    p.end();
  }
  sanitise(s);
  Lock l;
  current = s;
  Log::printf("[settings] HA %s (token %s), NAS %s, %s\n", s.haUrl.c_str(), s.haToken.length() ? "set" : "not set",
                s.nasHost.c_str(), s.adminPassword.length() ? "admin password set" : "no admin password");
}

SettingsData get() {
  Lock l;
  return current;
}

bool save(const SettingsData &s) {
  SettingsData v = s;
  sanitise(v);
  if (!write(v)) return false;
  Lock l;
  current = v;
  dirty = false;
  Log::println("[settings] saved");
  return true;
}

void set(const SettingsData &s) {
  SettingsData v = s;
  sanitise(v);
  Lock l;
  current = v;
  dirty = true;
  dirtyAtMs = millis();
}

void flush(uint32_t quietMs) {
  SettingsData v;
  {
    Lock l;
    if (!dirty || millis() - dirtyAtMs < quietMs) return;
    dirty = false;
    v = current;
  }
  Log::println(write(v) ? "[settings] saved (touch)" : "[settings] flash write failed");
}

bool inSleepWindow(const SettingsData &s, int m) {
  if (!s.sleepEnabled || s.sleepStartMin == s.sleepEndMin) return false;
  if (s.sleepStartMin < s.sleepEndMin) return m >= s.sleepStartMin && m < s.sleepEndMin;
  return m >= s.sleepStartMin || m < s.sleepEndMin;  // Crosses midnight.
}

const uint16_t FLIGHT_RADII[] = {5, 10, 15, 25, 40, 60, 100};
const uint8_t FLIGHT_RADII_COUNT = sizeof(FLIGHT_RADII) / sizeof(FLIGHT_RADII[0]);
const uint16_t PAGE_SECS[] = {5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 300, 600};
const uint8_t PAGE_SECS_COUNT = sizeof(PAGE_SECS) / sizeof(PAGE_SECS[0]);

uint16_t step(const uint16_t *list, uint8_t n, uint16_t v, int dir) {
  if (dir > 0) {
    for (uint8_t i = 0; i < n; ++i) {
      if (list[i] > v) return list[i];
    }
    return list[n - 1];
  }
  for (int i = n - 1; i >= 0; --i) {
    if (list[i] < v) return list[i];
  }
  return list[0];
}

bool checkAdmin(const String &attempt) {
  Lock l;
  return current.adminPassword.isEmpty() || attempt == current.adminPassword;
}

uint8_t sonosList(String *out, uint8_t max) {
  const String list = get().sonosHosts;
  uint8_t n = 0;
  int start = 0;
  while (start < static_cast<int>(list.length()) && n < max) {
    int comma = list.indexOf(',', start);
    if (comma < 0) comma = list.length();
    String h = list.substring(start, comma);
    h.trim();
    if (h.length()) out[n++] = h;
    start = comma + 1;
  }
  return n;
}

}  // namespace Settings
