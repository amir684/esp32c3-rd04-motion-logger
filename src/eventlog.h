#pragma once
#include <Arduino.h>
#include <functional>

// Circular event log stored in the raw "evlog" flash partition.
// Every event is written straight to flash, so nothing is lost on reset or battery removal.

#define EVT_FLAG_TIME_VALID 0x0001

struct EventRec {
    uint32_t seq;
    uint32_t ts;     // epoch seconds (UTC) if EVT_FLAG_TIME_VALID, otherwise seconds since that boot
    uint16_t boot;   // boot session id
    uint16_t flags;
    uint32_t magic;
};

namespace EventLog {
    // trustRtcCache: true after a deep-sleep wake (skips scanning the flash)
    bool begin(bool trustRtcCache);
    bool append(uint32_t ts, uint16_t boot, uint16_t flags);
    // Newest first. limit 0 = all. Callback returns false to stop.
    void forEachNewestFirst(uint32_t limit, const std::function<bool(const EventRec &)> &cb);
    uint32_t count();
    uint32_t capacity();
    bool clear();
}
