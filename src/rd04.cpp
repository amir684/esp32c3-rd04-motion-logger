#include "rd04.h"
#include <Wire.h>
#include <driver/gpio.h>
#include "config.h"

// Register values follow Ai-Thinker's reference driver (STM32F102_Rd-04) and the Rd-04 module manual.
static const uint8_t RD04_ADDR = 0x71;

static bool writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(RD04_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static int readReg(uint8_t reg) {
    Wire.beginTransmission(RD04_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom((int)RD04_ADDR, 1) != 1) return -1;
    return Wire.read();
}

static void write24(uint8_t firstReg, uint32_t v) {
    writeReg(firstReg, v & 0xFF);
    writeReg(firstReg + 1, (v >> 8) & 0xFF);
    writeReg(firstReg + 2, (v >> 16) & 0xFF);
}

bool rd04Configure() {
    gpio_hold_dis((gpio_num_t)PIN_RADAR_I2CEN);
    pinMode(PIN_RADAR_I2CEN, OUTPUT);
    digitalWrite(PIN_RADAR_I2CEN, HIGH);
    delay(5);

    Wire.begin(PIN_RADAR_SDA, PIN_RADAR_SCL, 100000);

    bool ok = false;
    for (int i = 0; i < 5; i++) {
        // bb_ctl[31:24]: manual threshold, read-only data update, detection enable
        if (writeReg(0x13, 0x9B) && readReg(0x13) >= 0) { ok = true; break; }
        delay(10);
    }

    if (ok) {
        writeReg(0x24, 0x03);                        // power mode controlled by register
        writeReg(0x04, 0x20);                        // pulse power supply mode (low power)
        writeReg(0x10, 0x20);                        // 1 kHz ADC sampling / pulse rate
        writeReg(0x03, 0x40 | (RD04_TX_POWER & 0x07));
        writeReg(0x1C, 0x21);                        // hold time set by register
        writeReg(0x18, RD04_THRESHOLD & 0xFF);
        writeReg(0x19, (RD04_THRESHOLD >> 8) & 0xFF);
        writeReg(0x1A, 0x55);                        // noise update threshold
        writeReg(0x1B, 0x01);
        write24(0x1D, (uint32_t)RD04_HOLD_MS * 32);    // t1: OUT high time @32kHz
        write24(0x20, (uint32_t)RD04_LOCKOUT_MS * 32); // t2: lockout time @32kHz
        writeReg(0x23, 0x0C);                        // OUT pin = detection result
    }

    Wire.end();
    rd04HoldI2cDisabled();
    return ok;
}

void rd04HoldI2cDisabled() {
    // Don't leave the I2C lines driven/pulled during sleep
    pinMode(PIN_RADAR_SDA, INPUT);
    pinMode(PIN_RADAR_SCL, INPUT);

    pinMode(PIN_RADAR_I2CEN, OUTPUT);
    digitalWrite(PIN_RADAR_I2CEN, LOW);
    gpio_hold_en((gpio_num_t)PIN_RADAR_I2CEN);
    gpio_deep_sleep_hold_en();
}
