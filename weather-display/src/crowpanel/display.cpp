/**
 * @file    display.cpp
 * @brief   LovyanGFX configuration for the CrowPanel 7" and touch bring-up.
 */
#include "display.h"
#include "log.h"

#include <Wire.h>

#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

#include "board_config.h"

namespace {

constexpr int PIN_SDA = 19;
constexpr int PIN_SCL = 20;
constexpr uint8_t PCA9557_ADDR = 0x18;
constexpr uint8_t GT911_ADDR_A = 0x5D;
constexpr uint8_t GT911_ADDR_B = 0x14;

/** @brief Device class wiring the RGB bus, panel, backlight and touch together. */
class LGFX_CrowPanel : public lgfx::LGFX_Device {
  lgfx::Bus_RGB bus_;
  lgfx::Panel_RGB panel_;
  lgfx::Light_PWM light_;
  lgfx::Touch_GT911 touch_;

 public:
  /** @param touchAddr GT911 address found by probing (0 = no touch). */
  explicit LGFX_CrowPanel(uint8_t touchAddr) {
    {
      auto cfg = panel_.config();
      cfg.memory_width = cfg.panel_width = Display::WIDTH;
      cfg.memory_height = cfg.panel_height = Display::HEIGHT;
      cfg.offset_x = cfg.offset_y = 0;
      panel_.config(cfg);
    }
    {
      auto cfg = bus_.config();
      cfg.panel = &panel_;
      const int8_t data[16] = {15, 7, 6, 5, 4,          // B0-B4
                               9, 46, 3, 8, 16, 1,      // G0-G5
                               14, 21, 47, 48, 45};     // R0-R4
      for (int i = 0; i < 16; ++i) cfg.pin_data[i] = data[i];
      cfg.pin_henable = 41;
      cfg.pin_vsync = 40;
      cfg.pin_hsync = 39;
      cfg.pin_pclk = 0;
      cfg.freq_write = CP_PCLK_HZ;
      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch = 40;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch = 13;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;
      bus_.config(cfg);
    }
    panel_.setBus(&bus_);
    {
      auto cfg = light_.config();
      cfg.pin_bl = 2;
      cfg.freq = 20000;
      cfg.pwm_channel = 7;
      light_.config(cfg);
    }
    panel_.light(&light_);
    if (touchAddr) {
      auto cfg = touch_.config();
      cfg.x_min = 0;
      cfg.x_max = Display::WIDTH - 1;
      cfg.y_min = 0;
      cfg.y_max = Display::HEIGHT - 1;
      cfg.pin_int = -1;
      cfg.pin_rst = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = CP_TOUCH_ROTATION;
      cfg.i2c_port = 1;
      cfg.pin_sda = PIN_SDA;
      cfg.pin_scl = PIN_SCL;
      cfg.freq = 400000;
      cfg.i2c_addr = touchAddr;
      touch_.config(cfg);
      panel_.setTouch(&touch_);
    }
    setPanel(&panel_);
  }
};

LGFX_CrowPanel *device = nullptr;
uint8_t gtAddr = 0;
bool expander = false;

/** @brief True if a device ACKs at @p addr on the Arduino Wire bus. */
bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

/** @brief Write one PCA9557 register. */
void pcaWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(PCA9557_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

/**
 * @brief On V3.0 boards the GT911 RST/INT lines go through a PCA9557. Pulse
 *        RST with INT held low so the controller comes up at address 0x5D.
 */
void resetTouchViaExpander() {
  pcaWrite(0x03, 0x00);  // Configuration: all pins outputs.
  pcaWrite(0x01, 0x00);  // RST (IO0) low, INT (IO1) low.
  delay(20);
  pcaWrite(0x01, 0x01);  // Release RST; INT still low selects 0x5D.
  delay(100);
  pcaWrite(0x03, 0x02);  // INT back to input so the GT911 can drive it.
  delay(50);
}

/** @brief Find the touch controller (resetting it via the expander if present). */
void detectTouch() {
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  expander = probe(PCA9557_ADDR);
  if (expander) resetTouchViaExpander();
  if (probe(GT911_ADDR_A)) gtAddr = GT911_ADDR_A;
  else if (probe(GT911_ADDR_B)) gtAddr = GT911_ADDR_B;

  Log::printf("[display] I2C:");
  for (uint8_t a = 1; a < 127; ++a) {
    if (probe(a)) Log::printf(" 0x%02X", a);
  }
  Log::printf("  -> PCA9557 %s, GT911 %s (0x%02X)\n", expander ? "yes" : "no", gtAddr ? "found" : "NOT found", gtAddr);
  Wire.end();  // LovyanGFX drives the touch bus itself from here on.
}

}  // namespace

namespace Display {

bool begin() {
  detectTouch();
  device = new LGFX_CrowPanel(gtAddr);
  if (!device->init()) {
    Log::println("[display] panel init FAILED");
    return false;
  }
  device->setRotation(0);
  device->setBrightness(CP_BACKLIGHT);
  device->fillScreen(TFT_BLACK);
  Log::printf("[display] %ldx%ld ready, pclk %lu Hz\n", static_cast<long>(device->width()),
                static_cast<long>(device->height()), static_cast<unsigned long>(CP_PCLK_HZ));
  return true;
}

lgfx::LGFX_Device &lcd() { return *device; }

bool touch(int &x, int &y) {
  if (!device || !gtAddr) return false;
  lgfx::touch_point_t tp;
  if (device->getTouch(&tp) == 0) return false;
  x = tp.x;
  y = tp.y;
#if CP_TOUCH_FLIP
  x = WIDTH - 1 - x;
  y = HEIGHT - 1 - y;
#endif
  return true;
}

void setBrightness(uint8_t level) {
  if (device) device->setBrightness(level);
}

uint8_t touchAddress() { return gtAddr; }

bool hasExpander() { return expander; }

}  // namespace Display
