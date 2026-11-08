/**
 * @file    theme.h
 * @brief   RGB565 colour palette shared by the UI and icon renderer.
 */
#pragma once

#include <stdint.h>

/** @brief Named RGB565 colours. */
namespace Theme {
constexpr uint16_t BG      = 0x0000;  ///< Screen background (black).
constexpr uint16_t BAR     = 0x10A6;  ///< Status bar background (dark navy).
constexpr uint16_t WHITE   = 0xFFFF;  ///< Primary text.
constexpr uint16_t LIGHT   = 0xC618;  ///< Secondary text / cloud fill.
constexpr uint16_t GREY    = 0x8410;  ///< Tertiary text / dark cloud.
constexpr uint16_t DKGREY  = 0x39E7;  ///< Inactive indicators.
constexpr uint16_t YELLOW  = 0xFFE0;  ///< Sun, lightning, condition text.
constexpr uint16_t ORANGE  = 0xFD20;  ///< Daily high temperature.
constexpr uint16_t RED     = 0xF8A2;  ///< Errors / low battery.
constexpr uint16_t GREEN   = 0x07E0;  ///< Healthy battery.
constexpr uint16_t CYAN    = 0x07FF;  ///< Daily low temperature / USB.
constexpr uint16_t BLUE    = 0x3D9F;  ///< Rain.
}  // namespace Theme
