#pragma once
#include <Arduino.h>

// Opens the access point + web page and blocks until the idle timeout or "sleep" from the page.
void runAccessPoint(uint16_t bootId);

// Offset (real epoch - logged ts) learned when the clock was synced during boot session `boot`.
bool timeOffsetForBoot(uint16_t boot, int64_t &offset);
