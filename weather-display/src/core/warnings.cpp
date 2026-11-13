/**
 * @file    warnings.cpp
 * @brief   Met Office warnings RSS download and lightweight parsing.
 *
 * @details The feed is small (well under 16 KB even on busy days), so it is
 *          read into a String and scanned with indexOf(); a full XML parser
 *          isn't needed.
 */
#include "warnings.h"
#include "log.h"

#include "config.h"
#include "https_get.h"

namespace {

/** @brief Feed URL for the configured region. */
const char *const URL = "https://www.metoffice.gov.uk/public/data/PWSCache/WarningsRSS/Region/" WARNING_REGION;

/** @brief Refuse to buffer more than this (protects heap if the feed changes). */
constexpr size_t MAX_FEED_BYTES = 32 * 1024;

/**
 * @brief  Text between <tag> and </tag> inside @p xml.
 * @return Empty string if the tag is missing.
 */
String tagText(const String &xml, const char *tag) {
  const String open = String('<') + tag + '>';
  const String close = String("</") + tag + '>';
  const int a = xml.indexOf(open);
  if (a < 0) return String();
  const int b = xml.indexOf(close, a);
  if (b < 0) return String();
  return xml.substring(a + open.length(), b);
}

/** @brief Decode the handful of XML entities the feed uses. */
void decodeEntities(String &s) {
  s.replace("&lt;", "<");
  s.replace("&gt;", ">");
  s.replace("&quot;", "\"");
  s.replace("&apos;", "'");
  s.replace("&#39;", "'");
  s.replace("&amp;", "&");  // Last, so "&amp;lt;" is not double-decoded.
}

/**
 * @brief  Parse one RSS <item> into @p w.
 * @return false if the item is not a recognisable warning or is filtered out.
 */
bool parseItem(const String &item, WeatherWarning &w) {
  String title = tagText(item, "title");
  String desc = tagText(item, "description");
  decodeEntities(title);
  decodeEntities(desc);

  // Optional county/area filter so only local warnings are shown.
  if (strlen(WARNING_AREA_FILTER) > 0 && desc.indexOf(WARNING_AREA_FILTER) < 0) return false;

  if (title.startsWith("Red")) w.level = WarningLevel::Red;
  else if (title.startsWith("Amber")) w.level = WarningLevel::Amber;
  else if (title.startsWith("Yellow")) w.level = WarningLevel::Yellow;
  else return false;

  // "Yellow warning of fog affecting ..." -> "Fog"
  const int of = title.indexOf("warning of ");
  const int aff = title.indexOf(" affecting");
  String hazard = (of >= 0 && aff > of) ? title.substring(of + 11, aff) : String("Weather");
  if (hazard.length()) hazard.setCharAt(0, toupper(hazard[0]));
  strlcpy(w.hazard, hazard.c_str(), sizeof(w.hazard));

  // "... valid from 0000 Sun 08 Nov to 1000 Sun 08 Nov" -> "0000 Sun 08 Nov - 1000 Sun 08 Nov"
  const int vf = desc.indexOf("valid from ");
  String validity = vf >= 0 ? desc.substring(vf + 11) : String();
  validity.replace(" to ", " - ");
  validity.trim();
  strlcpy(w.validity, validity.c_str(), sizeof(w.validity));
  return true;
}

}  // namespace

namespace Warnings {

bool fetch(WarningList &out) {
  String body;
  const bool ok = Https::get(URL, "warnings", [&](HTTPClient &http) {
    const int len = http.getSize();
    if (len > 0 && (size_t)len > MAX_FEED_BYTES) return false;
    body = http.getString();
    return body.indexOf("<rss") >= 0 && body.length() <= MAX_FEED_BYTES;
  });
  if (!ok) return false;

  WarningList list;
  int pos = 0;
  while ((pos = body.indexOf("<item>", pos)) >= 0) {
    const int end = body.indexOf("</item>", pos);
    if (end < 0) break;
    WeatherWarning w;
    if (parseItem(body.substring(pos, end), w)) {
      ++list.total;
      if (list.count < MAX_WARNINGS) list.items[list.count++] = w;
    }
    pos = end;
  }

  // Most severe first (insertion sort; at most MAX_WARNINGS items).
  for (int i = 1; i < list.count; ++i) {
    for (int j = i; j > 0 && list.items[j].level > list.items[j - 1].level; --j) {
      std::swap(list.items[j], list.items[j - 1]);
    }
  }

  list.valid = true;
  list.fetchedAtMs = millis();
  out = list;

  Log::printf("[warnings] region %s, filter '%s': %u active\n", WARNING_REGION, WARNING_AREA_FILTER, list.total);
  for (int i = 0; i < list.count; ++i) {
    Log::printf("[warnings]   %s %s: %s\n", levelName(list.items[i].level), list.items[i].hazard, list.items[i].validity);
  }
  return true;
}

const char *levelName(WarningLevel level) {
  switch (level) {
    case WarningLevel::Yellow: return "Yellow";
    case WarningLevel::Amber: return "Amber";
    case WarningLevel::Red: return "Red";
    default: return "";
  }
}

}  // namespace Warnings
