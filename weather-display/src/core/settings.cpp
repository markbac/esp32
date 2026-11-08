/**
 * @file    settings.cpp
 * @brief   NVS-backed runtime settings with compiled-in defaults.
 */
#include "settings.h"

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
  return d;
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
    s.adminPassword = p.getString("admin", s.adminPassword);
    p.end();
  }
  Lock l;
  current = s;
  Serial.printf("[settings] HA %s (token %s), NAS %s, %s\n", s.haUrl.c_str(), s.haToken.length() ? "set" : "not set",
                s.nasHost.c_str(), s.adminPassword.length() ? "admin password set" : "no admin password");
}

SettingsData get() {
  Lock l;
  return current;
}

bool save(const SettingsData &s) {
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
  p.putString("admin", s.adminPassword);
  p.end();
  Lock l;
  current = s;
  Serial.println("[settings] saved");
  return true;
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
