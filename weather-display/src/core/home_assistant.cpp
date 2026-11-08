/**
 * @file    home_assistant.cpp
 * @brief   Home Assistant template-API client.
 */
#include "home_assistant.h"

#include <ArduinoJson.h>

#include <algorithm>

#include "config.h"
#include "https_get.h"
#include "settings.h"

namespace {

/**
 * @brief Jinja template rendered by Home Assistant into compact JSON.
 *        Lists are [name, value, unit] arrays to keep the response small.
 */
const char *const TEMPLATE = R"JINJA({%- set ns = namespace(t=[], h=[], p=[], o=[], pe=[], c=[]) -%}
{%- for s in states.sensor -%}
  {%- set dc = s.attributes.device_class | default('') -%}
  {%- if s.state | is_number -%}
    {%- if dc == 'temperature' and ns.t | length < 16 -%}
      {%- set ns.t = ns.t + [[s.name, s.state | float | round(1), s.attributes.unit_of_measurement | default('')]] -%}
    {%- elif dc == 'humidity' and ns.h | length < 12 -%}
      {%- set ns.h = ns.h + [[s.name, s.state | float | round(0), '%']] -%}
    {%- elif dc == 'power' and ns.p | length < 40 -%}
      {%- set ns.p = ns.p + [[s.name, s.state | float | round(0), s.attributes.unit_of_measurement | default('W')]] -%}
    {%- endif -%}
  {%- endif -%}
{%- endfor -%}
{%- for s in states.binary_sensor -%}
  {%- if s.state == 'on' and (s.attributes.device_class | default('')) in ['door', 'window', 'opening', 'garage_door'] and ns.o | length < 8 -%}
    {%- set ns.o = ns.o + [s.name] -%}
  {%- endif -%}
{%- endfor -%}
{%- for s in states.person -%}{%- set ns.pe = ns.pe + [[s.name, s.state]] -%}{%- endfor -%}
{%- for s in states.climate -%}
  {%- if ns.c | length < 3 -%}
    {%- set ns.c = ns.c + [[s.name, s.attributes.current_temperature | default(none), s.attributes.temperature | default(none), s.attributes.hvac_action | default(s.state)]] -%}
  {%- endif -%}
{%- endfor -%}
{{ {"t": ns.t, "h": ns.h, "p": ns.p, "o": ns.o, "pe": ns.pe, "c": ns.c,
    "lo": states.light | selectattr('state', 'eq', 'on') | list | count, "lt": states.light | list | count} | tojson }})JINJA";

/** @brief Copy one [name, value, unit] row. */
void reading(JsonArray row, HaReading &r) {
  strlcpy(r.name, row[0] | "", sizeof(r.name));
  r.value = row[1] | 0.0f;
  strlcpy(r.unit, row[2] | "", sizeof(r.unit));
}

}  // namespace

namespace HomeAssistant {

bool configured() { return Settings::get().haToken.length() > 0; }

bool fetch(HaData &out) {
  HaData d;
  const SettingsData cfg = Settings::get();
  d.configured = cfg.haToken.length() > 0;
  if (!d.configured) {
    out = d;
    return true;  // Nothing to do; not an error.
  }

  JsonDocument req;
  req["template"] = TEMPLATE;
  String body;
  serializeJson(req, body);

  Https::Options opt;
  opt.http10 = false;
  opt.timeoutMs = 8000;
  opt.headers = {{"Authorization", "Bearer " + cfg.haToken}, {"Content-Type", "application/json"}};
  JsonDocument doc;
  int status = 0;
  const bool ok = Https::request(
      "POST", cfg.haUrl + "/api/template", "homeassistant", body,
      [&](HTTPClient &http) {
        const DeserializationError err = deserializeJson(doc, http.getString());
        if (err) Serial.printf("[homeassistant] JSON error: %s\n", err.c_str());
        return !err;
      },
      opt, &status);
  d.lastStatus = status;
  if (!ok) {
    out.lastStatus = status;  // Keep the last good data but report the error.
    out.configured = true;
    return false;
  }

  for (JsonArray r : doc["t"].as<JsonArray>()) if (d.tempCount < 16) reading(r, d.temps[d.tempCount++]);
  for (JsonArray r : doc["h"].as<JsonArray>()) if (d.humidityCount < 12) reading(r, d.humidity[d.humidityCount++]);
  // Power: keep the 12 largest.
  HaReading all[40];
  uint8_t n = 0;
  for (JsonArray r : doc["p"].as<JsonArray>()) if (n < 40) reading(r, all[n++]);
  std::sort(all, all + n, [](const HaReading &a, const HaReading &b) { return a.value > b.value; });
  for (uint8_t i = 0; i < n && d.powerCount < 12; ++i) d.power[d.powerCount++] = all[i];
  for (const char *o : doc["o"].as<JsonArray>()) if (d.openCount < 8) strlcpy(d.open[d.openCount++], o ? o : "", 28);
  for (JsonArray p : doc["pe"].as<JsonArray>()) {
    if (d.peopleCount >= 8) break;
    HaPerson &pp = d.people[d.peopleCount++];
    strlcpy(pp.name, p[0] | "", sizeof(pp.name));
    strlcpy(pp.state, p[1] | "", sizeof(pp.state));
  }
  for (JsonArray c : doc["c"].as<JsonArray>()) {
    if (d.climateCount >= 3) break;
    HaClimate &cc = d.climate[d.climateCount++];
    strlcpy(cc.name, c[0] | "", sizeof(cc.name));
    cc.current = c[1].isNull() ? NAN : c[1].as<float>();
    cc.target = c[2].isNull() ? NAN : c[2].as<float>();
    strlcpy(cc.action, c[3] | "", sizeof(cc.action));
  }
  d.lightsOn = doc["lo"] | 0;
  d.lightsTotal = doc["lt"] | 0;
  d.valid = true;
  d.fetchedAtMs = millis();
  out = d;
  Serial.printf("[homeassistant] %u temps, %u humidity, %u power, %u people, %u climate, lights %u/%u, %u open\n",
                d.tempCount, d.humidityCount, d.powerCount, d.peopleCount, d.climateCount, d.lightsOn, d.lightsTotal,
                d.openCount);
  return true;
}

}  // namespace HomeAssistant
