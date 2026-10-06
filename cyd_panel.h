#pragma once
#include <Arduino_GFX_Library.h>

// Arduino_GFX 1.5.9 ends its ILI9341 initialization with SLPOUT/DISPON
// back-to-back. Let the controller finish waking, then repeat DISPON.
// Rotation and the first address window are applied by begin() after this.
class CydIli9341 final : public Arduino_ILI9341 {
public:
  using Arduino_ILI9341::Arduino_ILI9341;
protected:
  void tftInit() override {
    Arduino_ILI9341::tftInit();
    delay(150);
    _bus->sendCommand(ILI9341_DISPON);
    delay(20);
  }
};
