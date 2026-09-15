#pragma once
#include <Arduino.h>

// Configures the RD-04 (Phosense XBR818) over I2C into pulse-power mode (~110uA instead of ~16mA).
// Registers are volatile: call this after every power-up of the radar.
bool rd04Configure();

// Drives IIC_EN low and latches it through deep sleep.
void rd04HoldI2cDisabled();
