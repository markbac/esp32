/**
 * @file    png_tile.h
 * @brief   Download PNG map tiles and decode them into an RGB565 frame buffer.
 *
 * @details Tiles are downloaded into a PSRAM buffer and decoded line by line
 *          with PNGdec, so a full RGBA tile is never held in RAM. Used for both
 *          the base map (restyled dark) and the radar layer (alpha-blended).
 */
#pragma once

#include <Arduino.h>

namespace PngTile {

/** @brief How decoded pixels are combined with the destination. */
enum class Mode : uint8_t {
  Opaque,      ///< Copy every pixel unchanged.
  DarkMap,     ///< Restyle a light OpenStreetMap tile as a dark map (inverted grey, navy water).
  AlphaBlend,  ///< Blend RGBA pixels over the destination by their alpha (radar layer).
};

/**
 * @brief  Download a URL into the shared PSRAM download buffer.
 * @param  url  https:// URL of a PNG.
 * @param  tag  Log label.
 * @param[out] data  Points at the downloaded bytes (valid until the next call).
 * @param[out] len   Number of bytes.
 * @return true on success.
 */
bool download(const String &url, const char *tag, uint8_t *&data, size_t &len);

/**
 * @brief  Decode a PNG and copy it into @p dst at the given offset, clipping to bounds.
 * @param  data     PNG bytes.
 * @param  len      Byte count.
 * @param  dst      Destination RGB565 buffer (little-endian, row-major).
 * @param  dstW     Destination width, pixels.
 * @param  dstH     Destination height, pixels.
 * @param  offX     X of the tile's top-left corner within @p dst (may be negative).
 * @param  offY     Y of the tile's top-left corner within @p dst (may be negative).
 * @param  mode     How pixels are combined with @p dst.
 * @return true if the image decoded.
 */
bool blit(uint8_t *data, size_t len, uint16_t *dst, int dstW, int dstH, int offX, int offY, Mode mode);

}  // namespace PngTile
