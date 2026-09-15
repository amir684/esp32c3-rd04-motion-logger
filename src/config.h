#pragma once

// ---------------- Wiring (ESP32-C3 SuperMini) ----------------
// RD-04 OUT -> must be GPIO0..GPIO5 (only these can wake the C3 from deep sleep)
#define PIN_RADAR_OUT   3

// Only VIN, GND and OUT are needed with the radar's factory settings.
// Set to 1 (and wire SDA/SCL/IIC_EN below) to apply the RD04_* tuning values at power-up.
#define RD04_USE_I2C    0
#define PIN_RADAR_SDA   6
#define PIN_RADAR_SCL   7
#define PIN_RADAR_I2CEN 10

// BOOT button is GPIO9, which can NOT wake the C3 from deep sleep.
// Put a jumper wire between header pins 9 and 4 -> BOOT then wakes the board via GPIO4.
#define PIN_BOOT        9
#define PIN_BOOT_WAKE   4   // set to -1 if you don't add the jumper

#define PIN_LED         8   // on-board blue LED, active LOW
#define EVENT_BLINK_MS  30  // short LED flash after an event is saved, 0 = off

// ---------------- Behaviour ----------------
// Every OUT pulse (LOW->HIGH) is one event; the radar keeps OUT high (~5s) while there is motion.
// A new pulse that starts less than this many seconds after the previous one ended is merged
// into it (not logged). 0 = log every pulse.
#define EVENT_MERGE_GAP_S   0

// Access point
#define AP_SSID             "RD04-Logger"
#define AP_PASSWORD         "12345678"   // min 8 chars, "" = open network
#define AP_IDLE_TIMEOUT_S   300          // go back to sleep after 5 min without requests

// ---------------- RD-04 radar tuning (only used when RD04_USE_I2C = 1) ----------------
#define RD04_TX_POWER       5      // 0 = strongest/longest range ... 7 = weakest
#define RD04_THRESHOLD      0x015A // detection threshold, higher = less sensitive
#define RD04_HOLD_MS        500    // how long OUT stays HIGH after a detection
#define RD04_LOCKOUT_MS     1000   // blind time after OUT goes LOW
