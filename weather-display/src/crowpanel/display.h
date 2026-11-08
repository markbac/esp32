/**
 * @file    display.h
 * @brief   CrowPanel 7" hardware: 800x480 RGB TFT, backlight and GT911 touch.
 *
 * @details Elecrow CrowPanel 7.0" Basic (DIS08070H), ESP32-S3-WROOM-1-N4R8.
 *          | Function      | GPIO                                             |
 *          |---------------|--------------------------------------------------|
 *          | RGB data B0-4 | 15, 7, 6, 5, 4                                   |
 *          | RGB data G0-5 | 9, 46, 3, 8, 16, 1                               |
 *          | RGB data R0-4 | 14, 21, 47, 48, 45                               |
 *          | DE/VS/HS/PCLK | 41 / 40 / 39 / 0                                 |
 *          | Backlight     | 2 (PWM)                                          |
 *          | Touch I2C     | SDA 19, SCL 20 (GT911 at 0x5D or 0x14)           |
 *          | PCA9557       | 0x18 on V3.0 boards: IO0 = touch RST, IO1 = INT  |
 *
 *          The panel's frame buffer lives in PSRAM and is scanned out
 *          continuously by the LCD_CAM peripheral.
 */
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace Display {

/** @brief Panel width and height in landscape. */
constexpr int WIDTH = 800;
constexpr int HEIGHT = 480;

/**
 * @brief  Reset/detect the touch controller, initialise the panel and backlight.
 * @return true if the panel initialised (touch may still be absent).
 */
bool begin();

/** @brief The LovyanGFX device (for pushSprite etc.). */
lgfx::LGFX_Device &lcd();

/**
 * @brief  Read the current touch point.
 * @param[out] x, y  Screen coordinates.
 * @return true while the screen is being touched.
 */
bool touch(int &x, int &y);

/** @brief Backlight 0-255. */
void setBrightness(uint8_t level);

/** @brief GT911 I2C address found at boot (0 if none). */
uint8_t touchAddress();

/** @brief True if a PCA9557 expander (V3.0 board) was found and used to reset touch. */
bool hasExpander();

}  // namespace Display
