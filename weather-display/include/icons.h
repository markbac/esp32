/**
 * @file    icons.h
 * @brief   Scalable vector weather icons drawn with Adafruit_GFX primitives.
 *
 * @details Icons are designed on a 48 x 48 grid centred on (0, 0) and scaled
 *          at draw time, so the same artwork is used for the large icon on the
 *          Now page and the small icons on the forecast pages. No bitmap
 *          assets are needed.
 */
#pragma once

#include <Adafruit_GFX.h>

/** @brief Visual icon categories that WMO codes are grouped into. */
enum class WeatherIcon : uint8_t {
  Clear,         ///< Sun (day) or moon (night).
  PartlyCloudy,  ///< Sun/moon behind a cloud.
  Cloudy,        ///< Overcast.
  Fog,           ///< Horizontal haze bars.
  Drizzle,       ///< Cloud with light dots.
  Rain,          ///< Cloud with rain streaks.
  Showers,       ///< Sun/moon, cloud and rain.
  Sleet,         ///< Freezing rain/drizzle: rain and snow mixed.
  Snow,          ///< Cloud with snowflakes.
  Thunder,       ///< Dark cloud with lightning bolt.
};

namespace Icons {

/** @brief Map a WMO weather code to an icon category. */
WeatherIcon fromWmo(int wmoCode);

/**
 * @brief Draw a weather icon.
 * @param gfx    Target surface (screen or off-screen canvas).
 * @param cx     Icon centre x, pixels.
 * @param cy     Icon centre y, pixels.
 * @param size   Bounding-box edge length, pixels (48 = native).
 * @param wmoCode WMO weather code.
 * @param isDay  Selects sun (true) or moon (false) artwork.
 */
void draw(Adafruit_GFX &gfx, int cx, int cy, int size, int wmoCode, bool isDay);

/**
 * @brief Half sun on a horizon with an up (sunrise) or down (sunset) arrow.
 * @param gfx     Target surface.
 * @param cx      Centre x.
 * @param cy      Horizon y.
 * @param size    Overall width, pixels.
 * @param rising  true = sunrise arrow (up), false = sunset arrow (down).
 */
void sunEvent(Adafruit_GFX &gfx, int cx, int cy, int size, bool rising);

/**
 * @brief Moon disc shaded for the given phase (northern-hemisphere view).
 * @param gfx    Target surface.
 * @param cx     Centre x.
 * @param cy     Centre y.
 * @param r      Radius, pixels.
 * @param phase  0 = new, 0.25 = first quarter (right half lit), 0.5 = full, 0.75 = last quarter.
 */
void moon(Adafruit_GFX &gfx, int cx, int cy, int r, float phase);

/**
 * @brief Aircraft silhouette rotated to a heading (an arrow below 12 px).
 * @param gfx      Target surface.
 * @param cx       Centre x.
 * @param cy       Centre y.
 * @param size     Wingspan, pixels.
 * @param heading  Degrees true; 0 points up (north).
 * @param color    RGB565 fill colour.
 */
void plane(Adafruit_GFX &gfx, int cx, int cy, int size, float heading, uint16_t color);

}  // namespace Icons
