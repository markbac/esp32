/**
 * @file    homelab.cpp
 * @brief   Glances, Speedtest Tracker, Uptime Kuma and Sonos clients.
 */
#include "homelab.h"

#include <ArduinoJson.h>

#include "config.h"
#include "https_get.h"
#include "settings.h"

namespace {

/** @brief GET a LAN JSON endpoint (HTTP/1.1, whole body) with an optional filter. */
bool getJson(const String &url, const char *tag, JsonDocument &doc, JsonDocument *filter = nullptr) {
  Https::Options opt;
  opt.http10 = false;
  opt.timeoutMs = 5000;
  opt.headers = {{"Accept", "application/json"}};
  return Https::request(
      "GET", url, tag, String(),
      [&](HTTPClient &http) {
        const String body = http.getString();
        return !(filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter)) : deserializeJson(doc, body));
      },
      opt);
}

/** @brief Append @p name to a comma-separated list if it fits. */
void appendName(char *list, size_t n, const char *name) {
  if (strlen(list) + strlen(name) + 3 >= n) return;
  if (list[0]) strlcat(list, ", ", n);
  strlcat(list, name, n);
}

void fetchNas(NasStatus &n, const SettingsData &cfg) {
  const String base = "http://" + cfg.nasHost + ":" + cfg.glancesPort + "/api/4/";
  JsonDocument doc;
  if (!getJson(base + "quicklook", "glances", doc)) return;
  n.cpu = doc["cpu"] | 0.0f;
  n.mem = doc["mem"] | 0.0f;
  n.swap = doc["swap"] | 0.0f;
  n.load = doc["load"] | 0.0f;
  strlcpy(n.cpuName, doc["cpu_name"] | "", sizeof(n.cpuName));
  n.ok = true;

  if (getJson(base + "system", "glances", doc)) strlcpy(n.host, doc["hostname"] | "", sizeof(n.host));
  if (getJson(base + "uptime", "glances", doc)) strlcpy(n.uptime, doc.as<const char *>() ? doc.as<const char *>() : "", sizeof(n.uptime));
  if (getJson(base + "fs", "glances", doc)) {
    for (JsonObject f : doc.as<JsonArray>()) {
      const float size = f["size"] | 0.0f;
      if (size / 1e12f > n.diskTb) {  // Largest filesystem.
        n.diskTb = size / 1e12f;
        n.disk = f["percent"] | 0.0f;
      }
    }
  }
  if (getJson(base + "sensors", "glances", doc)) {
    for (JsonObject s : doc.as<JsonArray>()) {
      const char *type = s["type"] | "";
      if (strstr(type, "temperature")) {
        const float v = s["value"] | NAN;
        if (isnan(n.temp) || v > n.temp) n.temp = v;
      }
    }
  }
  JsonDocument filter;
  filter[0]["name"] = true;
  filter[0]["status"] = true;
  if (getJson(base + "containers", "glances", doc, &filter)) {
    for (JsonObject c : doc.as<JsonArray>()) {
      ++n.containers;
      const char *st = c["status"] | "";
      if (!strcmp(st, "unhealthy") || !strcmp(st, "exited") || !strcmp(st, "dead") || !strcmp(st, "restarting")) {
        ++n.unhealthy;
        appendName(n.unhealthyNames, sizeof(n.unhealthyNames), c["name"] | "?");
      } else {
        ++n.healthy;
      }
    }
  }
}

/** @brief "2026-11-08T20:30:01.000000+00:00" (UTC) -> epoch, via days-from-civil. */
uint32_t parseIso(const char *s) {
  int y, mo, d, h, mi, se;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
  y -= mo <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int32_t days = era * 146097 + static_cast<int>(doe) - 719468;
  return static_cast<uint32_t>(days) * 86400UL + h * 3600UL + mi * 60UL + se;
}

void fetchSpeed(SpeedStatus &s, const SettingsData &cfg) {
  JsonDocument doc;
  // Speedtest Tracker (PHP) answers HTTP/1.1 slowly with chunked keep-alive; HTTP/1.0 is ~3x faster.
  Https::Options opt;
  opt.http10 = true;
  opt.timeoutMs = 12000;
  opt.headers = {{"Accept", "application/json"}};
  if (!Https::request("GET", "http://" + cfg.nasHost + ":" + cfg.speedtestPort + "/api/speedtest/latest", "speedtest",
                      String(), [&](HTTPClient &http) { return !deserializeJson(doc, http.getStream()); }, opt)) {
    return;
  }
  JsonObject d = doc["data"];
  if (d.isNull()) return;
  s.down = d["download"] | 0.0f;
  s.up = d["upload"] | 0.0f;
  s.ping = d["ping"] | 0.0f;
  s.testedAt = parseIso(d["created_at"] | "");
  strlcpy(s.server, d["server_name"] | "", sizeof(s.server));
  s.ok = true;
}

/**
 * @brief  Latest heartbeat status for monitor @p id, found by scanning the raw JSON.
 * @details The heartbeat document holds ~100 beats per monitor (hundreds of KB);
 *          building a JSON tree from it exhausts internal RAM, so only the
 *          monitor's array is located and its last "status" read.
 * @return 1 up, 0 down, 2 pending, 3 maintenance, -1 unknown.
 */
int lastStatus(const String &json, int id) {
  const String key = String("\"") + id + "\":[";
  const int start = json.indexOf(key);
  if (start < 0) return -1;
  const int end = json.indexOf(']', start + key.length());  // Beat objects contain no arrays.
  if (end < 0) return -1;
  const int st = json.lastIndexOf("\"status\":", end);
  if (st < start) return -1;
  return json.substring(st + 9, st + 11).toInt();
}

void fetchServices(ServicesStatus &s, const SettingsData &cfg) {
  const String base = "http://" + cfg.nasHost + ":" + cfg.kumaPort + "/api/status-page/";
  JsonDocument page;
  JsonDocument filter;
  filter["publicGroupList"][0]["monitorList"][0]["id"] = true;
  filter["publicGroupList"][0]["monitorList"][0]["name"] = true;
  if (!getJson(base + cfg.kumaSlug, "kuma", page, &filter)) return;

  String beats;  // Raw text (large Strings are allocated in PSRAM).
  Https::Options opt;
  opt.http10 = false;
  opt.timeoutMs = 8000;
  if (!Https::request("GET", base + "heartbeat/" + cfg.kumaSlug, "kuma", String(),
                      [&](HTTPClient &http) {
                        beats = http.getString();
                        return beats.length() > 0;
                      },
                      opt)) {
    return;
  }
  JsonArray groups = page["publicGroupList"];
  for (JsonObject g : groups) {
    JsonArray monitors = g["monitorList"];
    for (JsonObject m : monitors) {
      ++s.total;
      const int status = lastStatus(beats, m["id"] | 0);
      if (status == 1) {
        ++s.up;
      } else {
        ++s.down;
        appendName(s.downNames, sizeof(s.downNames), m["name"] | "?");
      }
    }
  }
  s.ok = s.total > 0;
}

/** @brief Text between @p open and @p close in @p xml (empty if absent). */
String between(const String &xml, const char *open, const char *close) {
  const int a = xml.indexOf(open);
  if (a < 0) return String();
  const int b = xml.indexOf(close, a + strlen(open));
  return b < 0 ? String() : xml.substring(a + strlen(open), b);
}

/** @brief Undo one level of XML entity escaping. */
void unescape(String &s) {
  s.replace("&lt;", "<");
  s.replace("&gt;", ">");
  s.replace("&quot;", "\"");
  s.replace("&apos;", "'");
  s.replace("&amp;", "&");
}

/** @brief POST a read-only SOAP query to a Sonos speaker. */
bool soap(const char *ip, const char *service, const char *action, String &out) {
  const String body = String("<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                             "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:") +
                      action + " xmlns:u=\"urn:schemas-upnp-org:service:" + service + ":1\"><InstanceID>0</InstanceID></u:" +
                      action + "></s:Body></s:Envelope>";
  Https::Options opt;
  opt.http10 = false;
  opt.timeoutMs = 4000;
  opt.headers = {{"Content-Type", "text/xml; charset=\"utf-8\""},
                 {"SOAPACTION", String("\"urn:schemas-upnp-org:service:") + service + ":1#" + action + "\""}};
  return Https::request("POST", String("http://") + ip + ":1400/MediaRenderer/" + service + "/Control", "sonos", body,
                        [&](HTTPClient &http) {
                          out = http.getString();
                          return true;
                        },
                        opt);
}

void fetchSonos(const String &ipStr, SonosStatus &s) {
  const char *ip = ipStr.c_str();
  static String rooms[MAX_SONOS], models[MAX_SONOS];  // Device description rarely changes: cache it.
  static String ips[MAX_SONOS];
  int slot = -1;
  for (int i = 0; i < MAX_SONOS; ++i) {
    if (ips[i] == ipStr || ips[i].isEmpty()) {
      slot = i;
      break;
    }
  }
  if (slot < 0) return;
  if (ips[slot].isEmpty()) {
    Https::Options opt;
    opt.http10 = false;
    opt.timeoutMs = 4000;
    String xml;
    if (Https::request("GET", String("http://") + ip + ":1400/xml/device_description.xml", "sonos", String(),
                       [&](HTTPClient &http) {
                         xml = http.getString();
                         return true;
                       },
                       opt)) {
      rooms[slot] = between(xml, "<roomName>", "</roomName>");
      models[slot] = between(xml, "<modelName>", "</modelName>");
      ips[slot] = ipStr;
    } else {
      return;
    }
  }
  strlcpy(s.room, rooms[slot].c_str(), sizeof(s.room));
  strlcpy(s.model, models[slot].c_str(), sizeof(s.model));

  String xml;
  if (!soap(ip, "AVTransport", "GetTransportInfo", xml)) return;
  strlcpy(s.state, between(xml, "<CurrentTransportState>", "</CurrentTransportState>").c_str(), sizeof(s.state));
  if (soap(ip, "AVTransport", "GetPositionInfo", xml)) {
    String meta = between(xml, "<TrackMetaData>", "</TrackMetaData>");
    unescape(meta);  // DIDL-Lite is XML-escaped inside the SOAP response.
    String title = between(meta, "<dc:title>", "</dc:title>");
    String artist = between(meta, "<dc:creator>", "</dc:creator>");
    unescape(title);
    unescape(artist);
    strlcpy(s.title, title.c_str(), sizeof(s.title));
    strlcpy(s.artist, artist.c_str(), sizeof(s.artist));
  }
  s.ok = true;
}

}  // namespace

namespace Homelab {

bool fetch(HomelabData &out) {
  HomelabData d;
  const SettingsData cfg = Settings::get();
  fetchNas(d.nas, cfg);
  fetchSpeed(d.speed, cfg);
  fetchServices(d.services, cfg);
  String hosts[MAX_SONOS];
  const uint8_t n = Settings::sonosList(hosts, MAX_SONOS);
  for (uint8_t i = 0; i < n; ++i) {
    fetchSonos(hosts[i], d.sonos[d.sonosCount]);
    ++d.sonosCount;
  }
  d.valid = d.nas.ok || d.speed.ok || d.services.ok;
  d.fetchedAtMs = millis();
  out = d;
  Serial.printf("[homelab] NAS %s cpu %.0f%% mem %.0f%% disk %.0f%% %.0fC, %u/%u containers ok; speed %.0f/%.0f Mbps; "
                "services %u/%u up; sonos %u\n",
                d.nas.ok ? "ok" : "--", d.nas.cpu, d.nas.mem, d.nas.disk, d.nas.temp, d.nas.healthy, d.nas.containers,
                d.speed.down, d.speed.up, d.services.up, d.services.total, d.sonosCount);
  return d.valid;
}

}  // namespace Homelab
