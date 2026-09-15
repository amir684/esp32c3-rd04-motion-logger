// ESP32-C3 SuperMini + RD-04 radar: battery motion logger.
//
// Normally the ESP is in deep sleep and the radar runs in its ~110uA pulse mode.
//  - Radar OUT goes HIGH -> wake, store an event in flash, sleep until OUT goes LOW
//  - Radar OUT goes LOW  -> wake briefly, sleep until the next HIGH
//  - BOOT (via GPIO4 jumper) or RST pressed -> open Wi-Fi AP with the event log page
// Wiring: RD-04 VIN->3V3, GND->GND, OUT->GPIO3 (I2C optional, see RD04_USE_I2C)
#include <Arduino.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "config.h"
#include "rd04.h"
#include "eventlog.h"
#include "webui.h"

#ifdef RD_DEBUG
#define DBG(...) Serial.printf(__VA_ARGS__)
#else
#define DBG(...)
#endif

static const time_t VALID_EPOCH = 1700000000;
static const uint32_t RTC_MAGIC = 0xB007C0DE;

RTC_DATA_ATTR static uint32_t rtcMagic = 0;
RTC_DATA_ATTR static uint16_t rtcBootId = 0;
RTC_DATA_ATTR static bool rtcWaitingForLow = false;
RTC_DATA_ATTR static time_t rtcLastPulseEnd = 0;

static bool radarHigh() {
    pinMode(PIN_RADAR_OUT, INPUT);
    return digitalRead(PIN_RADAR_OUT) == HIGH;
}

static void enableButtonWake() {
    if (PIN_BOOT_WAKE < 0) return;
    // Don't fall asleep while the button is still held (it would wake us straight away)
    pinMode(PIN_BOOT_WAKE, INPUT_PULLUP);
    for (int i = 0; i < 100 && digitalRead(PIN_BOOT_WAKE) == LOW; i++) delay(50);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_BOOT_WAKE, ESP_GPIO_WAKEUP_GPIO_LOW);
}

[[noreturn]] static void sleepNow() {
    DBG("sleep\n");
#ifdef RD_DEBUG
    Serial.flush();
#endif
    esp_deep_sleep_start();
    while (true) {}
}

[[noreturn]] static void sleepUntilRadarHigh();

[[noreturn]] static void sleepUntilRadarLow() {
    if (!radarHigh()) {
        rtcLastPulseEnd = time(nullptr);
        sleepUntilRadarHigh();
    }
    rtcWaitingForLow = true;
    esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_RADAR_OUT, ESP_GPIO_WAKEUP_GPIO_LOW);
    enableButtonWake();
    sleepNow();
}

[[noreturn]] static void sleepUntilRadarHigh() {
    // Already high (e.g. after the AP): wait for this pulse to end without logging it
    if (radarHigh()) sleepUntilRadarLow();
    rtcWaitingForLow = false;
    esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_RADAR_OUT, ESP_GPIO_WAKEUP_GPIO_HIGH);
    enableButtonWake();
    sleepNow();
}

static void logEvent() {
    time_t now = time(nullptr);
    uint16_t flags = now > VALID_EPOCH ? EVT_FLAG_TIME_VALID : 0;
    bool ok = EventLog::append((uint32_t)now, rtcBootId, flags);
    DBG("event ts=%ld ok=%d\n", (long)now, ok);

    if (ok && EVENT_BLINK_MS > 0) {
        pinMode(PIN_LED, OUTPUT);
        digitalWrite(PIN_LED, LOW);
        delay(EVENT_BLINK_MS);
        digitalWrite(PIN_LED, HIGH);
        pinMode(PIN_LED, INPUT);
    }
}

static bool resetOpensAccessPoint(esp_reset_reason_t rr) {
    // Crashes and brown-outs (flat battery) go straight back to sleep instead of draining it with Wi-Fi
    switch (rr) {
        case ESP_RST_BROWNOUT:
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return false;
        default:
            return true;
    }
}

[[noreturn]] static void coldBoot(esp_reset_reason_t rr) {
    Preferences p;
    p.begin("rd04", false);
    rtcBootId = p.getUShort("boot", 0) + 1;
    p.putUShort("boot", rtcBootId);
    p.end();
    rtcMagic = RTC_MAGIC;
    rtcWaitingForLow = false;
    rtcLastPulseEnd = 0;

#if RD04_USE_I2C
    bool radarOk = rd04Configure();
    DBG("cold boot, reason=%d boot=%u radar=%d\n", rr, rtcBootId, radarOk);
#else
    DBG("cold boot, reason=%d boot=%u\n", rr, rtcBootId);
#endif
    EventLog::begin(false);

    if (resetOpensAccessPoint(rr)) runAccessPoint(rtcBootId);
    sleepUntilRadarHigh();
}

void setup() {
#ifdef RD_DEBUG
    Serial.begin(115200);
    delay(1500);
#endif
    esp_reset_reason_t rr = esp_reset_reason();
    if (rr != ESP_RST_DEEPSLEEP || rtcMagic != RTC_MAGIC) coldBoot(rr);

    // The sleep code latches wake-up pins; release them so they can be reconfigured
    gpio_hold_dis((gpio_num_t)PIN_RADAR_OUT);
    if (PIN_BOOT_WAKE >= 0) gpio_hold_dis((gpio_num_t)PIN_BOOT_WAKE);

    EventLog::begin(true);

    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_GPIO) sleepUntilRadarHigh();

    uint64_t pins = esp_sleep_get_gpio_wakeup_status();
    pinMode(PIN_BOOT, INPUT_PULLUP);
    bool button = (PIN_BOOT_WAKE >= 0 && (pins & (1ULL << PIN_BOOT_WAKE))) || digitalRead(PIN_BOOT) == LOW;
    DBG("gpio wake pins=0x%llx button=%d waitingLow=%d\n", pins, button, rtcWaitingForLow);

    if (button) {
        runAccessPoint(rtcBootId);
        sleepUntilRadarHigh();
    }

    if (rtcWaitingForLow) {
        // Pulse ended
        rtcLastPulseEnd = time(nullptr);
        sleepUntilRadarHigh();
    }

    // New pulse started
    time_t now = time(nullptr);
    if (rtcLastPulseEnd == 0 || now - rtcLastPulseEnd >= EVENT_MERGE_GAP_S) logEvent();
    sleepUntilRadarLow();
}

void loop() {}
