/**
 * @file    radar.cpp
 * @brief   RainViewer + OpenStreetMap tile composition for the radar loop.
 *
 * @details The base map uses the standard OpenStreetMap tiles (no API key),
 *          restyled dark while decoding. They are fetched once per boot, in
 *          line with the OSM tile usage policy. Attribution is drawn on the map:
 *          (c) OpenStreetMap contributors, RainViewer.
 */
#include "radar.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include <functional>

#include "geo.h"
#include "https_get.h"
#include "png_tile.h"

namespace {

constexpr size_t FRAME_BYTES = RadarLoop::W * RadarLoop::H * sizeof(uint16_t);

uint16_t *basemap = nullptr;  ///< Cached base map (same viewport), PSRAM.
bool basemapReady = false;    ///< True once all base-map tiles decoded.

/** @brief A frame listed in RainViewer's index. */
struct FrameRef {
  time_t time;   ///< Observation time.
  String path;   ///< URL prefix "<host><path>".
};

/** @brief Builds the URL for tile (x, y) at RADAR_ZOOM. */
using TileUrl = std::function<String(int x, int y)>;

/** @brief Allocate from PSRAM, falling back to internal RAM. */
uint16_t *allocFrame() {
  void *p = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return static_cast<uint16_t *>(p ? p : malloc(FRAME_BYTES));
}

/**
 * @brief  Viewport's top-left corner in global pixel coordinates.
 * @param[out] ox Global pixel x.
 * @param[out] oy Global pixel y.
 */
void viewportOrigin(long &ox, long &oy) {
  double x, y;
  Geo::worldPixel(atof(WX_LATITUDE), atof(WX_LONGITUDE), RADAR_ZOOM, x, y);
  ox = static_cast<long>(x) - RadarLoop::W / 2;
  oy = static_cast<long>(y) - RadarLoop::H / 2;
}

/**
 * @brief  Download every tile overlapping the viewport and draw it into @p dst.
 * @param  dst      Frame buffer (W x H).
 * @param  url      Tile URL builder.
 * @param  mode     Pixel combination (dark base map or alpha-blended radar).
 * @param  tag      Log label.
 * @return true if every tile downloaded and decoded.
 */
bool composeTiles(uint16_t *dst, const TileUrl &url, PngTile::Mode mode, const char *tag) {
  long ox, oy;
  viewportOrigin(ox, oy);
  const long tx0 = ox / Geo::TILE_SIZE, tx1 = (ox + RadarLoop::W - 1) / Geo::TILE_SIZE;
  const long ty0 = oy / Geo::TILE_SIZE, ty1 = (oy + RadarLoop::H - 1) / Geo::TILE_SIZE;

  for (long ty = ty0; ty <= ty1; ++ty) {
    for (long tx = tx0; tx <= tx1; ++tx) {
      uint8_t *data;
      size_t len;
      if (!PngTile::download(url(tx, ty), tag, data, len)) return false;
      const int offX = static_cast<int>(tx * Geo::TILE_SIZE - ox);
      const int offY = static_cast<int>(ty * Geo::TILE_SIZE - oy);
      if (!PngTile::blit(data, len, dst, RadarLoop::W, RadarLoop::H, offX, offY, mode)) {
        Serial.printf("[%s] PNG decode failed for tile %ld/%ld\n", tag, tx, ty);
        return false;
      }
    }
  }
  return true;
}

/** @brief Download and cache the base map if not already done. */
bool ensureBasemap() {
  if (basemapReady) return true;
  if (!basemap) basemap = allocFrame();
  if (!basemap) return false;
  memset(basemap, 0, FRAME_BYTES);
  basemapReady = composeTiles(
      basemap,
      [](int x, int y) {
        return "https://tile.openstreetmap.org/" + String(RADAR_ZOOM) + "/" + x + "/" + y + ".png";
      },
      PngTile::Mode::DarkMap, "basemap");
  if (basemapReady) Serial.println("[radar] base map cached");
  return basemapReady;
}

/**
 * @brief  Read the newest RADAR_FRAMES frames from RainViewer's index.
 * @param[out] refs   Oldest first.
 * @param[out] count  Number filled.
 * @return true on success.
 */
bool listFrames(FrameRef (&refs)[RADAR_FRAMES], uint8_t &count) {
  JsonDocument filter;
  filter["host"] = true;
  filter["radar"]["past"][0]["time"] = true;
  filter["radar"]["past"][0]["path"] = true;

  JsonDocument doc;
  const bool ok = Https::get("https://api.rainviewer.com/public/weather-maps.json", "radar", [&](HTTPClient &http) {
    return !deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  });
  if (!ok) return false;

  const String host = doc["host"] | "https://tilecache.rainviewer.com";
  JsonArray past = doc["radar"]["past"];
  const size_t first = past.size() > RADAR_FRAMES ? past.size() - RADAR_FRAMES : 0;
  count = 0;
  for (size_t i = first; i < past.size(); ++i) {
    refs[count].time = past[i]["time"] | 0;
    refs[count].path = host + (past[i]["path"] | "");
    ++count;
  }
  return count > 0;
}

/** @brief Find a frame with time @p t in @p loop, or nullptr. */
const RadarFrame *findFrame(const RadarLoop &loop, time_t t) {
  for (uint8_t i = 0; i < loop.count; ++i) {
    if (loop.frames[i].time == t) return &loop.frames[i];
  }
  return nullptr;
}

}  // namespace

namespace Radar {

bool allocate(RadarLoop &loop) {
  for (RadarFrame &f : loop.frames) {
    if (!f.pixels) f.pixels = allocFrame();
    if (!f.pixels) return false;
  }
  return true;
}

bool fetch(RadarLoop &staging, const RadarLoop &current) {
  if (!staging.frames[RADAR_FRAMES - 1].pixels || !ensureBasemap()) return false;

  FrameRef refs[RADAR_FRAMES];
  uint8_t n = 0;
  if (!listFrames(refs, n)) return false;

  uint8_t reused = 0, downloaded = 0;
  staging.count = 0;
  for (uint8_t i = 0; i < n; ++i) {
    RadarFrame &dst = staging.frames[staging.count];
    if (const RadarFrame *have = findFrame(current, refs[i].time)) {
      memcpy(dst.pixels, have->pixels, FRAME_BYTES);  // Already composed earlier.
      ++reused;
    } else {
      memcpy(dst.pixels, basemap, FRAME_BYTES);
      const String &base = refs[i].path;
      const bool ok = composeTiles(
          dst.pixels,
          [&](int x, int y) { return base + "/256/" + RADAR_ZOOM + "/" + x + "/" + y + "/" RADAR_COLOR_SCHEME "/1_1.png"; },
          PngTile::Mode::AlphaBlend, "radar");
      if (!ok) continue;  // Skip a frame that failed; keep the rest of the loop.
      ++downloaded;
    }
    dst.time = refs[i].time;
    ++staging.count;
  }

  staging.valid = staging.count > 0;
  staging.fetchedAtMs = millis();
  Serial.printf("[radar] loop of %u frames (%u new, %u reused)\n", staging.count, downloaded, reused);
  return staging.valid;
}

}  // namespace Radar
