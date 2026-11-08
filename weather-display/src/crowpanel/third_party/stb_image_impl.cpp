/**
 * @file    stb_image_impl.cpp
 * @brief   Compiles stb_image (public domain, v2.30) for JPEG only, allocating in PSRAM.
 *
 * @details Used to decode aircraft photos from planespotters.net, which are
 *          progressive JPEGs (LovyanGFX's TJpgDec only handles baseline).
 */
#include <esp_heap_caps.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MALLOC(sz) heap_caps_malloc((sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_REALLOC(p, sz) heap_caps_realloc((p), (sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_FREE(p) free(p)
#include "stb_image.h"
