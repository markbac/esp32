/**
 * @file    web_settings.cpp
 * @brief   Settings form rendering, validation and saving.
 */
#include "web_settings.h"

#include <ArduinoJson.h>
#include <WebServer.h>

#include "config.h"
#include "data_service.h"
#include "settings.h"

namespace {

WebServer *srv = nullptr;
bool restartPending = false;

/** @brief Escape a value for an HTML attribute or text node. */
String esc(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += c;
    }
  }
  return o;
}

String field(const char *name, const char *label, const String &value, const char *type = "text", const char *hint = "") {
  return String("<label>") + label + "<input name=\"" + name + "\" type=\"" + type + "\" value=\"" + esc(value) +
         "\" autocomplete=\"off\"><small>" + hint + "</small></label>";
}

/** @brief Password input for a write-only secret: never pre-filled, shows set/not set, with a clear box. */
String secret(const char *name, const char *label, bool isSet, const char *hint) {
  return String("<label>") + label + " <span class=\"" + (isSet ? "set" : "unset") + "\">" + (isSet ? "set" : "not set") +
         "</span><input name=\"" + name + "\" type=\"password\" placeholder=\"" +
         (isSet ? "leave blank to keep" : "enter to set") + "\" autocomplete=\"new-password\"><small>" + hint +
         "</small></label><label class=\"inline\"><input type=\"checkbox\" name=\"clear_" + name + "\"> clear</label>";
}

void sendPage(const String &message, bool error) {
  const SettingsData s = Settings::get();
  String h;
  h.reserve(9000);
  h += F("<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>Settings</title><style>"
         ":root{--bg:#0d1117;--card:#161b22;--line:#30363d;--text:#e6edf3;--muted:#8b949e;--accent:#58a6ff}"
         "body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,sans-serif}"
         "main{max-width:760px;margin:0 auto;padding:20px}h1{font-size:20px}a{color:var(--accent)}"
         "fieldset{border:1px solid var(--line);border-radius:10px;margin:0 0 16px;padding:12px 16px;background:var(--card)}"
         "legend{color:var(--muted);font-weight:600;padding:0 6px}"
         "label{display:block;margin:10px 0}label.inline{display:inline-block;margin:0 0 6px;color:var(--muted);font-size:13px}"
         "input[type=text],input[type=password],input[type=number]{display:block;width:100%;box-sizing:border-box;margin-top:4px;"
         "padding:8px;border-radius:6px;border:1px solid var(--line);background:#0d1117;color:var(--text);font:inherit}"
         "small{color:var(--muted);font-size:12px}.set{color:#3fb950;font-size:12px}.unset{color:#8b949e;font-size:12px}"
         "button{background:var(--accent);color:#0d1117;border:0;border-radius:6px;padding:9px 16px;font:inherit;font-weight:600;"
         "cursor:pointer;margin-right:8px}button.alt{background:var(--card);color:var(--text);border:1px solid var(--line)}"
         ".msg{padding:10px 14px;border-radius:8px;margin-bottom:16px}.ok{background:#12361f}.err{background:#4a1d1d}"
         "</style></head><body><main>");
  h += "<h1>Settings &middot; " HOSTNAME ".local</h1><p><a href='/'>&larr; Dashboard</a></p>";
  if (message.length()) h += String("<div class='msg ") + (error ? "err" : "ok") + "'>" + esc(message) + "</div>";
  h += F("<form method=post action='/settings'>");

  h += F("<fieldset><legend>Home Assistant</legend>");
  h += field("ha_url", "Server URL", s.haUrl, "text", "e.g. http://10.0.10.244:8123");
  h += secret("ha_token", "Long-lived access token", s.haToken.length() > 0,
              "Profile &rarr; Security &rarr; Long-lived access tokens &rarr; Create token");
  h += F("</fieldset>");

  h += F("<fieldset><legend>Home lab</legend>");
  h += field("nas_host", "NAS host", s.nasHost, "text", "Runs Glances, Speedtest Tracker and Uptime Kuma");
  h += field("glances_port", "Glances port", String(s.glancesPort), "number");
  h += field("speedtest_port", "Speedtest Tracker port", String(s.speedtestPort), "number");
  h += field("kuma_port", "Uptime Kuma port", String(s.kumaPort), "number");
  h += field("kuma_slug", "Uptime Kuma status page slug", s.kumaSlug);
  h += field("sonos", "Sonos speakers", s.sonosHosts, "text", "Comma-separated IP addresses");
  h += F("</fieldset>");

  h += F("<fieldset><legend>Display</legend>");
  h += field("bl_day", "Backlight (0-255)", String(s.backlightDay), "number");
  h += field("bl_night", "Night backlight (0-255, 0 = no dimming)", String(s.backlightNight), "number",
             "Used from an hour after sunset until sunrise");
  h += F("</fieldset>");

  h += F("<fieldset><legend>Wi-Fi (applies after restart)</legend>");
  h += field("wifi_ssid", "Network name", s.wifiSsid, "text", "Blank = use the network the firmware was built with");
  h += secret("wifi_pass", "Password", s.wifiPassword.length() > 0, "2.4 GHz networks only");
  h += F("</fieldset>");

  h += F("<fieldset><legend>Protection</legend>");
  h += secret("admin_new", "Settings password", s.adminPassword.length() > 0,
              "When set, it is required to save changes on this page");
  if (s.adminPassword.length()) {
    h += F("<label>Current settings password<input name='admin' type='password' autocomplete='current-password' required></label>");
  }
  h += F("</fieldset>");

  h += F("<button type=submit name=action value=save>Save</button>"
         "<button class=alt type=submit name=action value=restart>Save &amp; restart</button>"
         "</form><p><small>Secrets are write-only: they are never shown or returned by the device.</small></p></main></body></html>");
  srv->sendHeader("Cache-Control", "no-store");
  srv->send(error ? 400 : 200, "text/html", h);
}

/** @brief Parse an integer field within [lo, hi]; returns false (and sets @p err) when invalid. */
bool intArg(const char *name, long lo, long hi, long &out, String &err) {
  const String v = srv->arg(name);
  if (v.isEmpty()) return true;  // Keep current.
  char *end = nullptr;
  const long n = strtol(v.c_str(), &end, 10);
  if (*end || n < lo || n > hi) {
    err = String(name) + " must be " + lo + "-" + hi;
    return false;
  }
  out = n;
  return true;
}

void handlePost() {
  if (!Settings::checkAdmin(srv->arg("admin"))) {
    sendPage("Settings password is wrong - nothing was saved.", true);
    return;
  }
  SettingsData s = Settings::get();
  String err;

  // Plain fields.
  String url = srv->arg("ha_url");
  url.trim();
  while (url.endsWith("/")) url.remove(url.length() - 1);
  if (url.length() && !url.startsWith("http://") && !url.startsWith("https://")) err = "Server URL must start with http:// or https://";
  else if (url.length()) s.haUrl = url;
  if (srv->hasArg("nas_host")) s.nasHost = srv->arg("nas_host");
  if (srv->hasArg("kuma_slug")) s.kumaSlug = srv->arg("kuma_slug");
  if (srv->hasArg("sonos")) s.sonosHosts = srv->arg("sonos");
  if (srv->hasArg("wifi_ssid")) s.wifiSsid = srv->arg("wifi_ssid");
  s.nasHost.trim();
  s.kumaSlug.trim();
  s.sonosHosts.trim();
  s.wifiSsid.trim();

  long v;
  v = s.glancesPort;
  if (err.isEmpty() && intArg("glances_port", 1, 65535, v, err)) s.glancesPort = v;
  v = s.speedtestPort;
  if (err.isEmpty() && intArg("speedtest_port", 1, 65535, v, err)) s.speedtestPort = v;
  v = s.kumaPort;
  if (err.isEmpty() && intArg("kuma_port", 1, 65535, v, err)) s.kumaPort = v;
  v = s.backlightDay;
  if (err.isEmpty() && intArg("bl_day", 5, 255, v, err)) s.backlightDay = v;
  v = s.backlightNight;
  if (err.isEmpty() && intArg("bl_night", 0, 255, v, err)) s.backlightNight = v;

  // Write-only secrets: blank keeps, "clear" empties, anything else replaces.
  auto applySecret = [&](const char *name, String &target) {
    if (srv->hasArg(String("clear_") + name)) target = "";
    else if (srv->arg(name).length()) target = srv->arg(name);
  };
  applySecret("ha_token", s.haToken);
  applySecret("wifi_pass", s.wifiPassword);
  applySecret("admin_new", s.adminPassword);

  if (err.length()) {
    sendPage(err + " - nothing was saved.", true);
    return;
  }
  if (!Settings::save(s)) {
    sendPage("Could not write settings to flash.", true);
    return;
  }
  DataService::refreshAll();  // Apply new hosts/token on the next worker tick.
  if (srv->arg("action") == "restart") {
    sendPage("Saved. Restarting now - reload this page in about 30 seconds.", false);
    restartPending = true;
  } else {
    sendPage("Saved. Changes apply within a few seconds.", false);
  }
}

/** @brief GET /api/settings: non-secret values plus set/not-set flags for secrets. */
void handleApi() {
  const SettingsData s = Settings::get();
  JsonDocument d;
  d["haUrl"] = s.haUrl;
  d["haToken"] = s.haToken.length() ? "set" : "not set";
  d["nasHost"] = s.nasHost;
  d["glancesPort"] = s.glancesPort;
  d["speedtestPort"] = s.speedtestPort;
  d["kumaPort"] = s.kumaPort;
  d["kumaSlug"] = s.kumaSlug;
  d["sonosHosts"] = s.sonosHosts;
  d["backlightDay"] = s.backlightDay;
  d["backlightNight"] = s.backlightNight;
  d["wifiSsid"] = s.wifiSsid;
  d["wifiPassword"] = s.wifiPassword.length() ? "set" : "not set";
  d["adminPassword"] = s.adminPassword.length() ? "set" : "not set";
  String out;
  serializeJson(d, out);
  srv->sendHeader("Cache-Control", "no-store");
  srv->send(200, "application/json", out);
}

}  // namespace

namespace WebSettings {

void registerRoutes(WebServer &server) {
  srv = &server;
  server.on("/settings", HTTP_GET, [] { sendPage("", false); });
  server.on("/settings", HTTP_POST, [] {
    handlePost();
    if (restartPending) {
      delay(800);  // Let the response reach the browser.
      ESP.restart();
    }
  });
  server.on("/api/settings", HTTP_GET, handleApi);
}

}  // namespace WebSettings
