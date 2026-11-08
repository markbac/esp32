/**
 * @file    aurora.cpp
 * @brief   AuroraWatch UK XML download and parsing.
 *
 * @details Both documents are under 4 KB and very regular, so they are read
 *          into a String and scanned with indexOf() rather than a full XML parser.
 */
#include "aurora.h"

#include "https_get.h"

namespace {

const char *const STATUS_URL = "https://aurorawatch-api.lancs.ac.uk/0.2/status/current-status.xml";
const char *const ACTIVITY_URL = "https://aurorawatch-api.lancs.ac.uk/0.2/status/alerting-site-activity.xml";
constexpr size_t MAX_XML = 16 * 1024;  ///< Sanity limit on document size.

/** @brief Map an AuroraWatch status_id ("green", "amber"...) to a level. */
AuroraLevel parseLevel(const String &id) {
  if (id == "green") return AuroraLevel::Green;
  if (id == "yellow") return AuroraLevel::Yellow;
  if (id == "amber") return AuroraLevel::Amber;
  if (id == "red") return AuroraLevel::Red;
  return AuroraLevel::Unknown;
}

/** @brief Value of attribute @p name (e.g. status_id="...") starting the search at @p from. */
String attribute(const String &xml, const char *name, int from) {
  const String key = String(name) + "=\"";
  const int a = xml.indexOf(key, from);
  if (a < 0) return String();
  const int b = xml.indexOf('"', a + key.length());
  return b < 0 ? String() : xml.substring(a + key.length(), b);
}

/** @brief GET a small XML document into @p body. */
bool getXml(const char *url, String &body) {
  return Https::get(url, "aurora", [&](HTTPClient &http) {
    body = http.getString();
    return body.length() > 0 && body.length() < MAX_XML && body.indexOf("<?xml") >= 0;
  });
}

}  // namespace

namespace Aurora {

bool fetch(AuroraData &out) {
  String xml;
  AuroraData a;

  // Overall status: <site_status ... status_id="green"/>
  if (!getXml(STATUS_URL, xml)) return false;
  const int site = xml.indexOf("<site_status");
  if (site < 0) return false;
  a.level = parseLevel(attribute(xml, "status_id", site));

  // Hourly activity: <activity status_id="amber"><datetime>..</datetime><value>106.1</value></activity>
  if (getXml(ACTIVITY_URL, xml)) {
    int pos = 0;
    while ((pos = xml.indexOf("<activity ", pos)) >= 0 && a.count < AURORA_HOURS) {
      const int v0 = xml.indexOf("<value>", pos);
      const int v1 = xml.indexOf("</value>", v0);
      if (v0 < 0 || v1 < 0) break;
      a.hourLevel[a.count] = parseLevel(attribute(xml, "status_id", pos));
      a.activity[a.count] = xml.substring(v0 + 7, v1).toFloat();
      ++a.count;
      pos = v1;
    }
  }

  a.valid = a.level != AuroraLevel::Unknown;
  a.fetchedAtMs = millis();
  if (!a.valid) return false;
  out = a;
  Serial.printf("[aurora] %s, latest %.1f nT, %u hourly samples\n", levelName(a.level), latestActivity(a), a.count);
  return true;
}

const char *levelName(AuroraLevel level) {
  switch (level) {
    case AuroraLevel::Green: return "Green";
    case AuroraLevel::Yellow: return "Yellow";
    case AuroraLevel::Amber: return "Amber";
    case AuroraLevel::Red: return "Red";
    default: return "?";
  }
}

const char *meaning(AuroraLevel level) {
  switch (level) {
    case AuroraLevel::Green: return "No significant activity";
    case AuroraLevel::Yellow: return "Minor activity";
    case AuroraLevel::Amber: return "Possible aurora, look N";
    case AuroraLevel::Red: return "Aurora likely! Look N";
    default: return "Status unknown";
  }
}

float latestActivity(const AuroraData &a) { return a.count ? a.activity[a.count - 1] : 0.0f; }

}  // namespace Aurora
