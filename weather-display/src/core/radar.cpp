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
#include "log.h"

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

/** @brief Builds the URL for tile (x, y) at RADAR_ZOOM. */
using TileUrl = std::function<String(int x, int y)>;

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
        Log::printf("[%s] PNG decode failed for tile %ld/%ld\n", tag, tx, ty);
        return false;
      }
    }
  }
  return true;
}

/** @brief Download and cache the base map if not already done. */
bool ensureBasemap() {
  if (basemapReady) return true;
  if (!basemap) basemap = Radar::allocFrame();
  if (!basemap) return false;
  memset(basemap, 0, FRAME_BYTES);
  basemapReady = composeTiles(
      basemap,
      [](int x, int y) {
        return "https://tile.openstreetmap.org/" + String(RADAR_ZOOM) + "/" + x + "/" + y + ".png";
      },
      PngTile::Mode::DarkMap, "basemap");
  if (basemapReady) Log::println("[radar] base map cached");
  return basemapReady;
}

}  // namespace

namespace Radar {

uint16_t *allocFrame() {
  void *p = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return static_cast<uint16_t *>(p ? p : malloc(FRAME_BYTES));
}

bool listFrames(RadarFrameRef (&refs)[RADAR_FRAMES], uint8_t &n) {
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
  n = 0;
  for (size_t i = first; i < past.size(); ++i) {
    refs[n].time = past[i]["time"] | 0;
    refs[n].path = host + (past[i]["path"] | "");
    ++n;
  }
  return n > 0;
}

bool compose(const RadarFrameRef &ref, uint16_t *dst) {
  if (!dst || !ensureBasemap()) return false;
  memcpy(dst, basemap, FRAME_BYTES);
  const String &base = ref.path;
  return composeTiles(
      dst, [&](int x, int y) { return base + "/256/" + RADAR_ZOOM + "/" + x + "/" + y + "/" RADAR_COLOR_SCHEME "/1_1.png"; },
      PngTile::Mode::AlphaBlend, "radar");
}

}  // namespace Radar

namespace {

constexpr int CELL = 4;                       ///< Pixels per grid cell.
constexpr int GW = RadarLoop::W / CELL;       ///< Grid width.
constexpr int GH = RadarLoop::H / CELL;       ///< Grid height.
constexpr int RADAR_ETA_SEARCH = 8;           ///< Max shift per frame, cells (~23 km).
constexpr int MIN_RAIN_CELLS = 12;            ///< Below this, motion is unreliable.

/** @brief Build the rain mask of @p frame: a cell is wet if >= 3 of its pixels differ from the base map. */
void rainMask(const uint16_t *frame, uint8_t *mask, int &count) {
  count = 0;
  for (int gy = 0; gy < GH; ++gy) {
    for (int gx = 0; gx < GW; ++gx) {
      int diff = 0;
      for (int y = 0; y < CELL; ++y) {
        const int row = (gy * CELL + y) * RadarLoop::W + gx * CELL;
        for (int x = 0; x < CELL; ++x) diff += frame[row + x] != basemap[row + x];
      }
      const bool wet = diff >= 3;
      mask[gy * GW + gx] = wet;
      count += wet;
    }
  }
}

/**
 * @brief  Best shift (dx, dy) carrying rain in @p a onto rain in @p b.
 * @return Fraction of @p a's wet cells that land on wet cells of @p b.
 */
float bestShift(const uint8_t *a, const uint8_t *b, int &bdx, int &bdy) {
  int best = -1, aCount = 0;
  for (int i = 0; i < GW * GH; ++i) aCount += a[i];
  bdx = bdy = 0;
  for (int dy = -RADAR_ETA_SEARCH; dy <= RADAR_ETA_SEARCH; ++dy) {
    for (int dx = -RADAR_ETA_SEARCH; dx <= RADAR_ETA_SEARCH; ++dx) {
      int hits = 0;
      for (int y = max(0, -dy); y < min(GH, GH - dy); ++y) {
        const uint8_t *ra = a + y * GW, *rb = b + (y + dy) * GW;
        for (int x = max(0, -dx); x < min(GW, GW - dx); ++x) hits += ra[x] & rb[x + dx];
      }
      // Prefer smaller shifts on ties (stationary showers).
      if (hits > best || (hits == best && dx * dx + dy * dy < bdx * bdx + bdy * bdy)) {
        best = hits;
        bdx = dx;
        bdy = dy;
      }
    }
    // Long, CPU-bound search on core 0: yield each row so the idle task (and the
    // task watchdog) are never starved - this reset the board once the Wi-Fi
    // tools added load on the same core.
    vTaskDelay(1);
  }
  return aCount ? static_cast<float>(best) / aCount : 0;
}

}  // namespace

namespace Radar {

RadarEta estimate(const RadarLoop &loop) {
  RadarEta eta;
  if (!basemapReady || loop.count < 2) return eta;

  static uint8_t *masks = nullptr;  // Up to 4 frames' masks, PSRAM.
  if (!masks) masks = static_cast<uint8_t *>(heap_caps_malloc(4 * GW * GH, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!masks) return eta;

  // Masks for the newest (up to) 4 frames, oldest first.
  const int n = min<int>(4, loop.count);
  int counts[4] = {};
  for (int i = 0; i < n; ++i) {
    rainMask(loop.frames[loop.count - n + i].pixels, masks + i * GW * GH, counts[i]);
    vTaskDelay(1);
  }
  const uint8_t *newest = masks + (n - 1) * GW * GH;
  eta.coverage = static_cast<float>(counts[n - 1]) / (GW * GH);

  // Rain over home (within one cell).
  const int hx = GW / 2, hy = GH / 2;
  for (int y = hy - 1; y <= hy + 1; ++y)
    for (int x = hx - 1; x <= hx + 1; ++x) eta.rainingHere |= newest[y * GW + x];

  // Average motion over consecutive pairs with enough rain to track.
  float vx = 0, vy = 0, minutes = 0;
  int pairs = 0;
  for (int i = 0; i + 1 < n; ++i) {
    if (counts[i] < MIN_RAIN_CELLS || counts[i + 1] < MIN_RAIN_CELLS) continue;
    int dx, dy;
    if (bestShift(masks + i * GW * GH, masks + (i + 1) * GW * GH, dx, dy) < 0.25f) continue;
    const float dt = (loop.frames[loop.count - n + i + 1].time - loop.frames[loop.count - n + i].time) / 60.0f;
    if (dt <= 0) continue;
    vx += dx;
    vy += dy;
    minutes += dt;
    ++pairs;
  }
  if (!pairs) return eta;
  vx /= minutes;  // Cells per minute.
  vy /= minutes;
  const float speed = sqrtf(vx * vx + vy * vy);

  const float kmPerPx = 156543.03f * cosf(atof(WX_LATITUDE) * DEG_TO_RAD) / (1UL << RADAR_ZOOM) / 1000.0f;
  const float kmPerCell = kmPerPx * CELL;
  eta.valid = true;
  eta.speedKmh = speed * kmPerCell * 60.0f;
  // Screen x = east, y = south; direction the rain comes FROM is opposite to motion.
  eta.fromDeg = (static_cast<int>(lroundf(atan2f(-vx, vy) / DEG_TO_RAD)) + 360) % 360;
  if (speed < 0.02f) return eta;  // Essentially stationary.

  // Walk upwind from home along -v, checking a corridor +/-2 cells wide.
  const float ux = -vx / speed, uy = -vy / speed;
  const int maxSteps = static_cast<int>(sqrtf(GW * GW + GH * GH) / 2);
  for (int s = 2; s < maxSteps; ++s) {
    for (int w = -2; w <= 2; ++w) {
      const int x = lroundf(hx + ux * s - uy * w), y = lroundf(hy + uy * s + ux * w);
      if (x < 0 || y < 0 || x >= GW || y >= GH) continue;
      if (newest[y * GW + x]) {
        eta.approaching = true;
        eta.distanceKm = s * kmPerCell;
        eta.minutes = static_cast<int>(lroundf(s / speed));
        return eta;
      }
    }
  }
  return eta;
}

}  // namespace Radar
