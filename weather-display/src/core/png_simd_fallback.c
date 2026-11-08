/**
 * @file    png_simd_fallback.c
 * @brief   Portable replacement for PNGdec's ESP32-S3 SIMD RGBA -> RGB565 routine.
 *
 * @details When ARDUINO_ESP32S3_DEV is defined (generic S3 board, as used for
 *          the CrowPanel) PNGdec calls s3_rgb565(), implemented in an assembly
 *          file that needs esp-dsp headers. scripts/skip_png_simd.py drops that
 *          file, so this C version provides the same function. Only used for
 *          RGBA lines decoded with the alpha channel ignored.
 */
#ifdef ARDUINO_ESP32S3_DEV

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Convert @p count RGBA8888 pixels to RGB565.
 * @param src        RGBA bytes (4 per pixel).
 * @param dst        Output, 2 bytes per pixel.
 * @param count      Pixel count.
 * @param bigEndian  Byte-swap the output.
 */
void s3_rgb565(uint8_t *src, uint8_t *dst, int count, bool bigEndian) {
  uint16_t *out = (uint16_t *)dst;
  for (int i = 0; i < count; ++i, src += 4) {
    uint16_t v = (uint16_t)(((src[0] & 0xF8) << 8) | ((src[1] & 0xFC) << 3) | (src[2] >> 3));
    out[i] = bigEndian ? __builtin_bswap16(v) : v;
  }
}

#endif
