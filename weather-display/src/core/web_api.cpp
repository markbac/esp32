/**
 * @file    web_api.cpp
 * @brief   Builds /api/state.json from the shared data store, serves cached photos.
 */
#include "web_api.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <esp_heap_caps.h>

#include "astro.h"
#include "config.h"
#include "connectivity.h"
#include "data_service.h"
#include "frost.h"
#include "history.h"

namespace {

WebServer *srv = nullptr;
const char *board = "";

/** @brief Round to @p d decimals (keeps the JSON short). */
float r1(float v, int d = 1) {
  if (isnan(v)) return v;
  const float k = d == 0 ? 1 : (d == 1 ? 10 : 100);
  return roundf(v * k) / k;
}

/**
 * @brief Set a member to a rounded float, or null when NaN.
 * @note  Takes the member proxy itself: converting a proxy for a missing key
 *        to JsonVariant does not create the member, so assignment must go
 *        through the proxy.
 */
template <typename Proxy>
void setNum(Proxy v, float x, int d = 1) {
  if (isnan(x)) v = nullptr;
  else v = r1(x, d);
}

void addNow(JsonObject o) {
  const TempestData &t = DataService::tempest();
  const WeatherData &wx = DataService::weather();
  if (t.fresh()) {
    o["source"] = "Tempest";
    setNum(o["temp"], t.obs.tempC);
    setNum(o["feels"], t.feelsC);
    setNum(o["humidity"], t.obs.humidity, 0);
    setNum(o["dew"], t.dewPointC);
    setNum(o["pressure"], t.obs.pressureSea);
    uint16_t mins = 0;
    setNum(o["trend"], t.pressureTrend(mins));
    o["trendMin"] = mins;
    setNum(o["windAvg"], t.obs.windAvg);
    setNum(o["windGust"], t.obs.windGust);
    setNum(o["windLull"], t.obs.windLull);
    o["windDir"] = t.obs.windDir;
    if (t.rapidValid && millis() - t.rapidAtMs < 30000) {
      setNum(o["liveMph"], t.rapidMph);
      o["liveDir"] = t.rapidDir;
    }
    setNum(o["rainToday"], t.rainTodayMm, 2);
    setNum(o["rainRate"], t.rainRateMmH);
    setNum(o["rainHour"], t.rainHourMm, 2);
    setNum(o["maxRateToday"], t.maxRateTodayMmH);
    o["intensity"] = Tempest::rainIntensity(t.rainRateMmH);
    setNum(o["uv"], t.obs.uv);
    setNum(o["solar"], t.obs.solar, 0);
    setNum(o["lux"], t.obs.lux, 0);
    o["strikesToday"] = t.strikesToday;
    o["strikesHour"] = t.strikesHour;
    if (t.lastStrikeTime) {
      o["lastStrikeKm"] = r1(t.lastStrikeKm, 0);
      o["lastStrikeTime"] = t.lastStrikeTime;
    }
    o["agoS"] = (millis() - t.obsAtMs) / 1000;
    // 3-hour trends (1-minute samples, every 5th to keep it small).
    JsonArray tt = o["trendTemp"].to<JsonArray>(), tp = o["trendPress"].to<JsonArray>(), tw = o["trendWind"].to<JsonArray>();
    for (int i = 0; i < t.trendCount; i += 5) {
      tt.add(r1(t.trendTemp[i]));
      tp.add(r1(t.trendPress[i]));
      tw.add(r1(t.trendWind[i]));
    }
  } else if (wx.valid) {
    const CurrentWeather &c = wx.current;
    o["source"] = "Open-Meteo";
    setNum(o["temp"], c.temp);
    setNum(o["feels"], c.feelsLike);
    setNum(o["humidity"], c.humidity, 0);
    setNum(o["pressure"], c.pressure);
    setNum(o["windAvg"], c.wind);
    setNum(o["windGust"], c.gust);
    o["windDir"] = c.windDir;
  }
}

void addForecast(JsonObject o) {
  const WeatherData &wx = DataService::weather();
  if (!wx.valid) return;
  JsonObject c = o["current"].to<JsonObject>();
  c["code"] = wx.current.code;
  c["desc"] = Weather::describe(wx.current.code);
  c["isDay"] = wx.current.isDay;
  setNum(c["temp"], wx.current.temp);
  setNum(c["feels"], wx.current.feelsLike);
  o["sunrise"] = wx.sunrise;
  o["sunset"] = wx.sunset;
  o["daylightS"] = static_cast<uint32_t>(wx.daylightSec);
  o["sunshineS"] = static_cast<uint32_t>(wx.sunshineSec);
  JsonArray h = o["hourly"].to<JsonArray>();
  for (int i = 0; i < wx.hourlyCount; ++i) {
    const HourlyPoint &p = wx.hourly[i];
    JsonObject x = h.add<JsonObject>();
    x["h"] = p.hour;
    x["t"] = r1(p.temp);
    x["code"] = p.code;
    x["day"] = p.isDay;
    x["rain"] = p.rainChance;
    x["wind"] = r1(p.wind, 0);
  }
  JsonArray d = o["daily"].to<JsonArray>();
  for (int i = 0; i < wx.dailyCount; ++i) {
    const DailyPoint &p = wx.daily[i];
    JsonObject x = d.add<JsonObject>();
    x["wd"] = p.weekday;
    x["max"] = r1(p.tMax);
    x["min"] = r1(p.tMin);
    x["code"] = p.code;
    x["rain"] = p.rainChance;
    x["uv"] = r1(p.uvMax, 0);
  }
  JsonObject n = o["nowcast"].to<JsonObject>();
  n["first"] = wx.nowcast.firstSlot;
  JsonArray mm = n["mm"].to<JsonArray>();
  for (int i = 0; i < wx.nowcast.count; ++i) mm.add(r1(wx.nowcast.mm[i], 2));
  tm t;
  if (Net::localTime(t)) {
    const RainSoon rs = Weather::rainSoon(wx, t.tm_hour * 60 + t.tm_min);
    static const char *const states[] = {"unknown", "dry", "starting", "raining"};
    n["state"] = states[static_cast<int>(rs.state)];
    n["minutes"] = rs.minutes;
  }
}

void addRadar(JsonObject o) {
  const RadarLoop &r = DataService::radar();
  const RadarEta &e = r.eta;
  o["frames"] = r.count;
  if (r.count) o["newest"] = static_cast<uint32_t>(r.frames[r.count - 1].time);
  JsonObject eta = o["eta"].to<JsonObject>();
  eta["valid"] = e.valid;
  eta["rainingHere"] = e.rainingHere;
  eta["approaching"] = e.approaching;
  eta["minutes"] = e.minutes;
  eta["distanceKm"] = r1(e.distanceKm, 0);
  eta["speedKmh"] = r1(e.speedKmh, 0);
  eta["fromDeg"] = e.fromDeg;
  eta["coverage"] = r1(e.coverage * 100, 1);
}

void addAir(JsonObject o) {
  const AirQualityData &a = DataService::air();
  if (!a.valid) return;
  o["eaqi"] = a.eaqi;
  o["band"] = AirQuality::band(a.eaqi);
  o["bandIndex"] = AirQuality::bandIndex(a.eaqi);
  o["pm25"] = r1(a.pm25);
  o["pm10"] = r1(a.pm10);
  o["no2"] = r1(a.no2);
  o["o3"] = r1(a.o3);
  o["uv"] = r1(a.uv);
  const WeatherData &wx = DataService::weather();
  if (wx.dailyCount) o["uvMax"] = r1(wx.daily[0].uvMax);
  const char *type = nullptr;
  o["pollen"] = AirQuality::pollen(a, type);
  if (type) o["pollenType"] = type;
  o["grass"] = r1(a.grass, 0);
  o["birch"] = r1(a.birch, 0);
  o["alder"] = r1(a.alder, 0);
}

void addGrid(JsonObject o) {
  const CarbonData &c = DataService::carbon();
  if (!c.valid) return;
  o["now"] = c.nowGPerKwh;
  o["index"] = Carbon::indexName(c.nowIndex);
  JsonObject mix = o["mix"].to<JsonObject>();
  for (int i = 0; i < static_cast<int>(Fuel::Count); ++i) mix[Carbon::fuelName(static_cast<Fuel>(i))] = r1(c.mix[i]);
  JsonArray f = o["forecast"].to<JsonArray>();
  for (int i = 0; i < c.count; ++i) {
    JsonArray s = f.add<JsonArray>();
    s.add(c.forecast[i].from);
    s.add(c.forecast[i].gPerKwh);
    s.add(static_cast<int>(c.forecast[i].index));
  }
  const int best = c.greenestWindow(6, 48);
  if (best >= 0) {
    uint32_t sum = 0;
    for (int k = 0; k < 6; ++k) sum += c.forecast[best + k].gPerKwh;
    JsonObject g = o["greenest"].to<JsonObject>();
    g["from"] = c.forecast[best].from;
    g["to"] = c.forecast[best].from + 3 * 3600;
    g["avg"] = sum / 6;
  }
}

void addAlerts(JsonObject o) {
  const WarningList &wl = DataService::warnings();
  o["area"] = WARNING_AREA_FILTER;
  o["warningsValid"] = wl.valid;
  JsonArray w = o["warnings"].to<JsonArray>();
  for (int i = 0; i < wl.count; ++i) {
    JsonObject x = w.add<JsonObject>();
    x["level"] = Warnings::levelName(wl.items[i].level);
    x["hazard"] = wl.items[i].hazard;
    x["validity"] = wl.items[i].validity;
  }
  o["total"] = wl.total;
  const AuroraData &a = DataService::aurora();
  if (a.valid) {
    JsonObject au = o["aurora"].to<JsonObject>();
    au["level"] = Aurora::levelName(a.level);
    au["meaning"] = Aurora::meaning(a.level);
    au["nT"] = r1(Aurora::latestActivity(a), 0);
    JsonArray hrs = au["hours"].to<JsonArray>();
    for (int i = 0; i < a.count; ++i) {
      JsonArray h = hrs.add<JsonArray>();
      h.add(r1(a.activity[i], 0));
      h.add(Aurora::levelName(a.hourLevel[i]));
    }
  }
  const FrostRisk fr = Frost::assess(DataService::weather(), DataService::tempest());
  JsonObject f = o["frost"].to<JsonObject>();
  f["level"] = Frost::levelName(fr.level);
  // fr is a local: force copies (ArduinoJson stores const char[N] by pointer).
  f["headline"] = String(fr.headline);
  f["detail"] = String(fr.detail);
}

void addSky(JsonObject o) {
  const MoonInfo m = Astro::moon(time(nullptr));
  JsonObject moon = o["moon"].to<JsonObject>();
  moon["phase"] = r1(m.phase, 2);
  moon["name"] = m.name;
  moon["illum"] = r1(m.illumination * 100, 0);
  moon["age"] = r1(m.ageDays);
  moon["nextFull"] = static_cast<uint32_t>(m.nextFull);
  moon["nextNew"] = static_cast<uint32_t>(m.nextNew);
}

void addFlights(JsonObject o) {
  const FlightList &fl = DataService::flights();
  o["radiusNm"] = FLIGHT_RADIUS_NM;
  o["total"] = fl.total;
  if (!fl.valid) return;
  if (fl.nearest >= 0) o["nearestHex"] = fl.items[fl.nearest].hex;
  JsonArray arr = o["aircraft"].to<JsonArray>();
  for (int i = 0; i < fl.count; ++i) {
    const Aircraft &a = fl.items[i];
    JsonObject x = arr.add<JsonObject>();
    x["hex"] = a.hex;
    x["cs"] = a.callsign;
    x["reg"] = a.reg;
    x["type"] = a.type;
    x["desc"] = a.desc;
    if (a.onGround) x["ground"] = true;
    else x["alt"] = a.altFt;
    x["gs"] = a.speedKt;
    x["trk"] = a.track;
    x["vr"] = a.vertRate;
    x["sq"] = a.squawk;
    x["dist"] = r1(a.distNm);
    x["brg"] = r1(a.bearing, 0);
    x["lat"] = a.lat;
    x["lon"] = a.lon;
    JsonArray tr = x["trail"].to<JsonArray>();
    for (int k = 0; k < a.trailCount; ++k) {
      JsonArray p = tr.add<JsonArray>();
      p.add(r1(a.trailDist[k], 2));
      p.add(r1(a.trailBearing[k], 0));
    }
    FlightRoute route;
    const bool have = (i == fl.nearest && strcmp(fl.route.callsign, a.callsign) == 0) ? (route = fl.route, true)
                                                                                        : Flights::findRoute(a.callsign, route);
    if (have) {
      JsonObject r = x["route"].to<JsonObject>();
      r["known"] = route.known;
      if (route.known) {
        r["from"] = route.from;
        r["to"] = route.to;
        r["fromCity"] = route.fromCity;
        r["toCity"] = route.toCity;
        r["airline"] = route.airline;
      }
    }
    x["photo"] = Flights::withPhoto(a.hex, [](const uint8_t *, size_t, const char *) {});
  }
}

void addHome(JsonObject o) {
  const HaData &h = DataService::homeAssistant();
  o["configured"] = h.configured;
  o["ok"] = h.valid;
  o["status"] = h.lastStatus;
  if (!h.valid) return;
  auto rows = [](JsonArray a, const HaReading *r, int n) {
    for (int i = 0; i < n; ++i) {
      JsonArray x = a.add<JsonArray>();
      x.add(r[i].name);
      x.add(r1(r[i].value));
      x.add(r[i].unit);
    }
  };
  rows(o["temps"].to<JsonArray>(), h.temps, h.tempCount);
  rows(o["humidity"].to<JsonArray>(), h.humidity, h.humidityCount);
  rows(o["power"].to<JsonArray>(), h.power, h.powerCount);
  JsonArray p = o["people"].to<JsonArray>();
  for (int i = 0; i < h.peopleCount; ++i) {
    JsonArray x = p.add<JsonArray>();
    x.add(h.people[i].name);
    x.add(h.people[i].state);
  }
  JsonArray c = o["climate"].to<JsonArray>();
  for (int i = 0; i < h.climateCount; ++i) {
    JsonObject x = c.add<JsonObject>();
    x["name"] = h.climate[i].name;
    setNum(x["current"], h.climate[i].current);
    setNum(x["target"], h.climate[i].target);
    x["action"] = h.climate[i].action;
  }
  o["lightsOn"] = h.lightsOn;
  o["lightsTotal"] = h.lightsTotal;
  JsonArray op = o["open"].to<JsonArray>();
  for (int i = 0; i < h.openCount; ++i) op.add(h.open[i]);
}

void addLab(JsonObject o) {
  const HomelabData &d = DataService::homelab();
  if (!d.valid) return;
  if (d.nas.ok) {
    JsonObject n = o["nas"].to<JsonObject>();
    n["host"] = d.nas.host;
    n["cpuName"] = d.nas.cpuName;
    n["uptime"] = d.nas.uptime;
    n["cpu"] = r1(d.nas.cpu);
    n["mem"] = r1(d.nas.mem);
    n["swap"] = r1(d.nas.swap);
    n["disk"] = r1(d.nas.disk);
    n["diskTb"] = r1(d.nas.diskTb, 2);
    setNum(n["temp"], d.nas.temp, 0);
    n["load"] = r1(d.nas.load, 0);
    n["containers"] = d.nas.containers;
    n["healthy"] = d.nas.healthy;
    n["unhealthy"] = d.nas.unhealthyNames;
  }
  if (d.speed.ok) {
    JsonObject s = o["speed"].to<JsonObject>();
    s["failed"] = d.speed.failed;
    s["down"] = r1(d.speed.down);
    s["up"] = r1(d.speed.up);
    s["ping"] = r1(d.speed.ping);
    s["at"] = d.speed.testedAt;
    s["server"] = d.speed.server;
  }
  if (d.services.ok) {
    JsonObject s = o["services"].to<JsonObject>();
    s["total"] = d.services.total;
    s["up"] = d.services.up;
    s["down"] = d.services.downNames;
  }
  JsonArray so = o["sonos"].to<JsonArray>();
  for (int i = 0; i < d.sonosCount; ++i) {
    const SonosStatus &s = d.sonos[i];
    if (!s.ok) continue;
    JsonObject x = so.add<JsonObject>();
    x["room"] = s.room;
    x["model"] = s.model;
    x["state"] = s.state;
    x["title"] = s.title;
    x["artist"] = s.artist;
  }
}

void addSystem(JsonObject o) {
  o["board"] = board;
  o["host"] = HOSTNAME;
  o["ip"] = Net::ip();
  o["ssid"] = Net::ssid();
  o["rssi"] = Net::rssi();
  o["uptimeS"] = millis() / 1000;
  o["heapKb"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
  o["psramKb"] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
  o["historyRecords"] = History::count();
  o["historyDays"] = HISTORY_DAYS;
  o["lat"] = atof(WX_LATITUDE);
  o["lon"] = atof(WX_LONGITUDE);
  o["now"] = static_cast<uint32_t>(time(nullptr));
  const TempestData &t = DataService::tempest();
  if (t.packets) {
    JsonObject ts = o["tempest"].to<JsonObject>();
    ts["hub"] = t.hubSerial;
    ts["hubFw"] = t.hubFirmware;
    ts["hubIp"] = t.hubIp;
    ts["hubRssi"] = t.hubWifiRssi;
    ts["hubUptimeS"] = t.hubUptime;
    ts["sensor"] = t.sensorSerial;
    ts["sensorFw"] = t.sensorFirmware;
    ts["battery"] = r1(t.sensorVoltage, 2);
    ts["sensorRssi"] = t.sensorRssi;
    ts["sensorUptimeS"] = t.sensorUptime;
    ts["status"] = t.sensorStatus;
    ts["packets"] = t.packets;
    ts["fresh"] = t.fresh();
  }
  DataService::FeedAge feeds[12];
  const uint8_t n = DataService::feedStatus(feeds, 12);
  JsonArray f = o["feeds"].to<JsonArray>();
  for (uint8_t i = 0; i < n; ++i) {
    JsonObject x = f.add<JsonObject>();
    x["name"] = feeds[i].name;
    x["ok"] = feeds[i].lastOk;
    if (feeds[i].okAtMs) x["agoS"] = (millis() - feeds[i].okAtMs) / 1000;
  }
}

void handleState() {
  JsonDocument doc;
  String out;
  {
    DataService::Guard g;
    addNow(doc["now"].to<JsonObject>());
    addForecast(doc["forecast"].to<JsonObject>());
    addRadar(doc["radar"].to<JsonObject>());
    addAir(doc["air"].to<JsonObject>());
    addGrid(doc["grid"].to<JsonObject>());
    addAlerts(doc["alerts"].to<JsonObject>());
    addSky(doc["sky"].to<JsonObject>());
    addFlights(doc["flights"].to<JsonObject>());
    addHome(doc["home"].to<JsonObject>());
    addLab(doc["lab"].to<JsonObject>());
    addSystem(doc["system"].to<JsonObject>());
    // Serialise while still locked: many strings are stored by pointer into the shared store.
    serializeJson(doc, out);
  }
  // External strings (aircraft, Sonos, Home Assistant names) may contain control
  // bytes, which ArduinoJson writes raw. Compact JSON has no whitespace of its
  // own, so any byte < 0x20 is inside a string: make it a space to keep JSON valid.
  for (unsigned i = 0; i < out.length(); ++i) {
    if (static_cast<uint8_t>(out[i]) < 0x20) out.setCharAt(i, ' ');
  }
  srv->sendHeader("Cache-Control", "no-store");
  srv->send(200, "application/json", out);
}

void handlePhoto() {
  const String hex = srv->arg("hex");
  uint8_t *copy = nullptr;
  size_t len = 0;
  const int r = Flights::withPhoto(hex.c_str(), [&](const uint8_t *jpg, size_t n, const char *) {
    copy = static_cast<uint8_t *>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (copy) {
      memcpy(copy, jpg, n);  // Copy out so the cache lock isn't held while sending.
      len = n;
    }
  });
  if (r < 0) {
    Flights::requestPhoto(hex.c_str());
    srv->send(202, "text/plain", "fetching");
    return;
  }
  if (!copy) {
    srv->send(404, "text/plain", "no photo");
    return;
  }
  srv->sendHeader("Cache-Control", "max-age=86400");
  srv->setContentLength(len);
  srv->send(200, "image/jpeg", "");
  srv->sendContent(reinterpret_cast<const char *>(copy), len);
  free(copy);
}

void handleLookup() {
  if (srv->hasArg("hex")) Flights::requestPhoto(srv->arg("hex").c_str());
  if (srv->hasArg("cs")) Flights::requestRoute(srv->arg("cs").c_str());
  srv->send(202, "text/plain", "queued");
}

}  // namespace

namespace WebApi {

void registerRoutes(WebServer &server, const char *boardName) {
  srv = &server;
  board = boardName;
  server.on("/api/state.json", HTTP_GET, handleState);
  server.on("/api/photo", HTTP_GET, handlePhoto);
  server.on("/api/lookup", HTTP_GET, handleLookup);
}

}  // namespace WebApi
