/**
 * @file    png_tile.cpp
 * @brief   Implementation of PNG tile download and decode.
 */
#include "png_tile.h"

#include <PNGdec.h>
#include <esp_heap_caps.h>

#include <new>

#include "https_get.h"

namespace {

constexpr size_t DOWNLOAD_MAX = 96 * 1024;  ///< Largest tile accepted (typical: 2-40 KB).
constexpr uint8_t ALPHA_THRESHOLD = 96;     ///< Mask cut-off for non-RGBA overlays.
constexpr uint8_t ALPHA_MIN = 12;           ///< RGBA pixels fainter than this are skipped.
constexpr uint32_t READ_TIMEOUT_MS = 10000; ///< Give up if the body stalls this long.

using PngTile::Mode;

/** @brief Pack 8-bit channels into RGB565. */
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }

/** @brief Unpack RGB565 into 8-bit channels. */
inline void unpack(uint16_t c, uint8_t &r, uint8_t &g, uint8_t &b) {
  r = (c >> 8) & 0xF8;
  g = (c >> 3) & 0xFC;
  b = (c << 3) & 0xF8;
}

/**
 * @brief Restyle one light OpenStreetMap pixel for a dark theme.
 * @details Water (clearly blue) becomes navy; everything else becomes an
 *          inverted, compressed grey so land is near-black, roads/borders are
 *          slightly lighter and dark label text turns light grey.
 */
uint16_t darkMapPixel(uint16_t c) {
  uint8_t r, g, b;
  unpack(c, r, g, b);
  if (b > r + 25 && b > 180) return rgb565(15, 30, 55);
  const uint8_t lum = (r * 77 + g * 150 + b * 29) >> 8;
  const uint8_t v = static_cast<uint8_t>((255 - lum) * 0.55f) + 18;
  return rgb565(v, v, v);
}

uint8_t *downloadBuf = nullptr;  ///< PSRAM download buffer, allocated on first use.
PNG *png = nullptr;              ///< Decoder (~50 KB), placed in PSRAM.

/** @brief Allocate from PSRAM, falling back to internal RAM. */
void *bigAlloc(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}

/** @brief Lazily allocate the download buffer and decoder. */
bool ensureBuffers() {
  if (!downloadBuf) downloadBuf = static_cast<uint8_t *>(bigAlloc(DOWNLOAD_MAX));
  if (!png) {
    void *mem = bigAlloc(sizeof(PNG));
    if (mem) png = new (mem) PNG();
  }
  return downloadBuf && png;
}

/** @brief Per-decode state handed to the PNGdec line callback. */
struct BlitContext {
  uint16_t *dst;        ///< Destination buffer.
  int dstW, dstH;       ///< Destination size.
  int offX, offY;       ///< Tile origin within destination.
  Mode mode;            ///< How to combine pixels.
  uint16_t line[256];   ///< One decoded line, RGB565.
  uint8_t mask[32];     ///< One line of alpha mask, 1 bit per pixel, MSB first.
};

/** @brief Blend an 8-bit RGBA line over the destination row (true alpha compositing). */
void blendRgba(const BlitContext *c, const PNGDRAW *d, uint16_t *row, int w) {
  const uint8_t *s = d->pPixels;
  for (int x = 0; x < w; ++x, s += 4) {
    const int dx = c->offX + x;
    const uint8_t a = s[3];
    if (a < ALPHA_MIN || dx < 0 || dx >= c->dstW) continue;
    uint8_t r, g, b;
    unpack(row[dx], r, g, b);
    const uint16_t ia = 255 - a;
    row[dx] = rgb565((s[0] * a + r * ia) / 255, (s[1] * a + g * ia) / 255, (s[2] * a + b * ia) / 255);
  }
}

/** @brief PNGdec callback: combine one decoded line with the destination. */
int drawLine(PNGDRAW *d) {
  auto *c = static_cast<BlitContext *>(d->pUser);
  const int y = c->offY + d->y;
  if (y < 0 || y >= c->dstH) return 1;
  const int w = min(d->iWidth, 256);
  uint16_t *row = c->dst + y * c->dstW;

  // Radar tiles are 8-bit RGBA: blend using the real alpha channel.
  if (c->mode == Mode::AlphaBlend && d->iPixelType == PNG_PIXEL_TRUECOLOR_ALPHA && d->iBpp == 8) {
    blendRgba(c, d, row, w);
    return 1;
  }

  png->getLineAsRGB565(d, c->line, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);
  const bool masked = c->mode == Mode::AlphaBlend;  // Other formats: on/off transparency.
  if (masked && !png->getAlphaMask(d, c->mask, ALPHA_THRESHOLD)) return 1;  // Fully transparent line.

  for (int x = 0; x < w; ++x) {
    const int dx = c->offX + x;
    if (dx < 0 || dx >= c->dstW) continue;
    if (masked && !(c->mask[x >> 3] & (0x80 >> (x & 7)))) continue;
    row[dx] = c->mode == Mode::DarkMap ? darkMapPixel(c->line[x]) : c->line[x];
  }
  return 1;
}

}  // namespace

namespace PngTile {

bool download(const String &url, const char *tag, uint8_t *&data, size_t &len) {
  if (!ensureBuffers()) {
    Serial.printf("[%s] out of memory for tile buffers\n", tag);
    return false;
  }
  size_t n = 0;
  const bool ok = Https::get(url, tag, [&](HTTPClient &http) {
    const int expected = http.getSize();
    if (expected > static_cast<int>(DOWNLOAD_MAX)) return false;
    auto *s = http.getStreamPtr();  // NetworkClient* on Arduino-ESP32 3.x.
    uint32_t lastData = millis();
    while (n < DOWNLOAD_MAX && (http.connected() || s->available())) {
      const size_t avail = s->available();
      if (avail) {
        n += s->readBytes(downloadBuf + n, min(avail, DOWNLOAD_MAX - n));
        lastData = millis();
        if (expected > 0 && n >= static_cast<size_t>(expected)) break;
      } else {
        if (millis() - lastData > READ_TIMEOUT_MS) return false;
        delay(2);
      }
    }
    return n > 8 && memcmp(downloadBuf, "\x89PNG", 4) == 0;
  });
  data = downloadBuf;
  len = n;
  return ok;
}

bool blit(uint8_t *data, size_t len, uint16_t *dst, int dstW, int dstH, int offX, int offY, Mode mode) {
  if (!ensureBuffers()) return false;
  BlitContext *ctx = static_cast<BlitContext *>(bigAlloc(sizeof(BlitContext)));
  if (!ctx) return false;
  *ctx = BlitContext{dst, dstW, dstH, offX, offY, mode, {}, {}};

  bool ok = png->openRAM(data, static_cast<int>(len), drawLine) == PNG_SUCCESS;
  if (ok) {
    ok = png->decode(ctx, 0) == PNG_SUCCESS;
    png->close();
  }
  free(ctx);
  return ok;
}

}  // namespace PngTile
