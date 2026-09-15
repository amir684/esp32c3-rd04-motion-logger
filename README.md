# ESP32-C3 + RD-04 Motion Logger

A battery-powered motion logger built from an **ESP32-C3 SuperMini** and an **Ai-Thinker RD-04** 5.8 GHz
Doppler radar module. The board sleeps almost all of the time, wakes up only when the radar sees
movement, writes one record straight to flash, and goes back to sleep. Press the button and it turns
into a Wi-Fi access point serving a small web page with the whole event log, plus a CSV download.

No cloud, no Wi-Fi credentials, no SD card — the log lives in a dedicated raw flash partition and
survives resets, firmware crashes and battery swaps.

<!-- Add images/board.jpg to show the assembled hardware here -->
<!-- ![The assembled logger](images/board.jpg) -->

---

## Contents

- [Why radar](#why-radar)
- [Features](#features)
- [Hardware](#hardware)
  - [Bill of materials](#bill-of-materials)
  - [Wiring](#wiring)
  - [The GPIO9 to GPIO4 jumper](#the-gpio9-to-gpio4-jumper)
- [Build and flash](#build-and-flash)
- [Using it](#using-it)
- [How it works](#how-it-works)
  - [Sleep / wake state machine](#sleep--wake-state-machine)
  - [The event log](#the-event-log)
  - [Timestamps without an RTC](#timestamps-without-an-rtc)
  - [Radar configuration over I2C](#radar-configuration-over-i2c)
- [Configuration reference](#configuration-reference)
- [HTTP API](#http-api)
- [Project layout](#project-layout)
- [Power consumption](#power-consumption)
- [Troubleshooting](#troubleshooting)
- [License](#license)

---

## Why radar

A PIR sensor needs line of sight and reacts to warm bodies. The RD-04 is a 5.8 GHz Doppler radar
(Phosense XBR818 inside): it detects *movement* through plastic, glass, drywall and doors, does not
care about temperature, and — once put into its pulse-power mode — draws roughly **110 µA**, which is
low enough to run the whole thing from a battery for a long time.

The module has a single digital output: `OUT` goes HIGH while there is motion and stays HIGH for a
configurable hold time after the last detection. That is all the firmware needs.

## Features

- **Deep sleep by default.** The ESP32-C3 only runs for a few milliseconds per event.
- **Crash-proof log.** Events are written directly to a raw flash partition (`evlog`), not to NVS or
  a filesystem. A reset, a brown-out or pulling the battery cannot lose entries.
- **Circular buffer.** 16,384 events in 256 KB; the oldest sector is erased when it wraps.
- **Captive-portal web UI.** Connect to the AP and any URL opens the log page — no IP to remember.
- **Time without an RTC.** The browser syncs the clock when you open the page; events logged before
  that are back-corrected using a per-boot offset stored in NVS (see
  [Timestamps without an RTC](#timestamps-without-an-rtc)).
- **CSV export** of the full log from the page.
- **Safe reset handling.** A brown-out or panic reset goes straight back to sleep instead of opening
  Wi-Fi and finishing off an already weak battery.
- **Single-header configuration** — every pin, timing and radar parameter lives in
  [`src/config.h`](src/config.h).

---

## Hardware

### Bill of materials

| Part | Notes |
| --- | --- |
| ESP32-C3 SuperMini | Electrically the same as the Seeed XIAO ESP32-C3 (4 MB flash, native USB). The firmware builds for the `seeed_xiao_esp32c3` board. |
| Ai-Thinker RD-04 | 5.8 GHz radar module, Phosense XBR818 chip. |
| Battery | Any 3.3 V-capable supply. |
| 1 jumper wire | Between header pins **9** and **4** — see below. |

### Wiring

Only three wires are needed with the radar's factory settings:

| RD-04 | ESP32-C3 SuperMini | Notes |
| --- | --- | --- |
| `VIN` | `3V3` | |
| `GND` | `GND` | |
| `OUT` | `GPIO3` | **Must** be GPIO0–GPIO5 — only those can wake the C3 from deep sleep. |

Optional, only when `RD04_USE_I2C` is enabled (see
[Radar configuration over I2C](#radar-configuration-over-i2c)):

| RD-04 | ESP32-C3 SuperMini |
| --- | --- |
| `SDA` | `GPIO6` |
| `SCL` | `GPIO7` |
| `IIC_EN` | `GPIO10` |

<!-- Add images/wiring.png and uncomment: -->
<!-- ![Wiring diagram](images/wiring.png) -->

### The GPIO9 to GPIO4 jumper

The BOOT button on the SuperMini is wired to **GPIO9**, and GPIO9 *cannot* wake the ESP32-C3 from
deep sleep. Running a single jumper wire between header pins **9** and **4** makes every BOOT press
also pull GPIO4 low, and GPIO4 *can* wake the chip. This is what lets you open the web UI without
resetting the board.

If you would rather not add the jumper, set `PIN_BOOT_WAKE` to `-1` in [`src/config.h`](src/config.h)
and use the **RST** button instead — a reset also opens the access point (at the cost of losing the
in-RAM clock).

---

## Build and flash

The project is a standard [PlatformIO](https://platformio.org/) project.

```bash
git clone https://github.com/amir684/esp32c3-rd04-motion-logger.git
cd esp32c3-rd04-motion-logger

pio run                 # build
pio run -t upload       # build + flash over USB
pio device monitor      # serial monitor @115200 (only useful with RD_DEBUG, see below)
```

Or open the folder in VS Code with the PlatformIO extension and press **Upload**.

The custom partition table in [`partitions.csv`](partitions.csv) is applied automatically, so the
first upload also lays out the `evlog` partition.

**Debug logging** is off by default because the serial output slows down every wake-up. To enable it,
uncomment `-DRD_DEBUG=1` in [`platformio.ini`](platformio.ini).

---

## Using it

1. Power the board. On the very first boot it opens the access point right away.
2. Connect to the Wi-Fi network:
   - SSID: **`RD04-Logger`**
   - Password: **`12345678`**
3. Your phone should open the captive portal automatically. If not, browse to any address
   (for example `http://192.168.4.1`).
4. The page shows the stored event count, today's count, the last event, and a day-grouped table.
   Opening the page also sets the device clock from your phone.
5. Buttons on the page: **refresh**, **download CSV**, **clear the log**, **sleep now**.
6. The AP closes on another BOOT press, on "sleep now", or after 5 minutes without requests
   (`AP_IDLE_TIMEOUT_S`). The blue LED blinks slowly the whole time the AP is open.
7. Back in sleep mode, the LED flashes for 30 ms each time an event is stored
   (`EVENT_BLINK_MS`, set to `0` to disable it and save the power).

> The web page is currently in **Hebrew**. It is a single string literal in
> [`src/index_html.h`](src/index_html.h) — translating it is a find-and-replace away.

<!-- Add images/webui.png and uncomment: -->
<!-- ![The event log page](images/webui.png) -->

---

## How it works

### Sleep / wake state machine

All of the logic lives in [`src/main.cpp`](src/main.cpp) — there is no `loop()`, every wake-up runs
`setup()` once and ends in `esp_deep_sleep_start()`. State that has to survive sleep is kept in
`RTC_DATA_ATTR` variables.

```
            +--------------------------------+
            |  deep sleep, armed for OUT HIGH |
            +---------------+----------------+
                            |  motion: OUT goes HIGH
                            v
            +--------------------------------+
            |  append event to flash          |
            |  (+ short LED flash)            |
            +---------------+----------------+
                            v
            +--------------------------------+
            |  deep sleep, armed for OUT LOW  |
            +---------------+----------------+
                            |  pulse ended: OUT goes LOW
                            v
                  remember pulse end time
                            |
                            +---> back to the top

   BOOT press (GPIO4) at any point ---> open access point ---> back to the top
```

Because the radar holds `OUT` high for several seconds of continuous motion, one "pulse" is one
event. `EVENT_MERGE_GAP_S` lets you merge pulses that start shortly after the previous one ended, so
a person walking around in front of the sensor does not produce dozens of rows.

Reset reason matters: `ESP_RST_BROWNOUT`, `ESP_RST_PANIC` and the watchdog resets skip the access
point and go straight back to sleep, so a dying battery is not drained further by Wi-Fi.

### The event log

[`src/eventlog.cpp`](src/eventlog.cpp) implements a circular log over the raw `evlog` partition.

```
partitions.csv
  nvs      data nvs      0x9000   0x6000    (boot counter, time offsets)
  factory  app  factory  0x10000  0x1E0000  (firmware)
  evlog    data 0x40     0x1F0000 0x40000   (the event log)
```

- Each record is 16 bytes: `seq`, `ts`, `boot`, `flags`, `magic` (`"RD04"`).
- 4096-byte sector / 16 = **256 records per sector**, 64 sectors = **16,384 events**.
- Writing only erases a sector when the write index crosses into it, so a normal event costs one
  16-byte flash write.
- The current head index is cached in RTC memory across deep sleep. After a cold boot the cache is
  rebuilt by scanning the partition for the highest sequence number — the partition is memory-mapped
  (`esp_partition_mmap`) for that, so the scan is a plain memory read.
- When the buffer wraps, the oldest sector (256 events) is erased.

### Timestamps without an RTC

The ESP32-C3 has no battery-backed clock, and this project deliberately never connects to the
internet. So:

- When the system clock looks valid (epoch > 2023-11), the event is stored with a real UTC timestamp
  and the `EVT_FLAG_TIME_VALID` flag.
- When it does not (right after a cold boot), the event is stored with *seconds since boot* plus the
  **boot session id** — a counter in NVS incremented on every cold boot.
- When you later open the page, the browser posts its own clock to `/api/time`. If the device clock
  was still invalid at that moment, the firmware computes `real_epoch - uptime` and stores it in NVS
  under the key `o<bootId>`.
- From then on, `/api/events` adds that offset to every record of that boot session and reports it as
  a valid timestamp. Events logged *before* you ever synced the clock are retroactively dated.

Events from a boot session that was never synced are shown as `+N seconds since boot N` in the table
and with `time_valid=0` in the CSV.

### Radar configuration over I2C

Out of the box the RD-04 already works in a usable low-power mode, so `RD04_USE_I2C` is **0** by
default and only three wires are needed.

Setting it to `1` makes the firmware program the XBR818 registers on every cold boot
([`src/rd04.cpp`](src/rd04.cpp)): pulse-power mode, 1 kHz sampling, TX power, detection threshold,
`OUT` hold time (`t1`) and lockout time (`t2`). The register values follow Ai-Thinker's reference
driver and the module manual. The chip's registers are volatile, which is why this runs after every
power-up rather than once. Afterwards `IIC_EN` is driven low and latched through deep sleep so the
I2C lines are not left driven.

---

## Configuration reference

Everything is in [`src/config.h`](src/config.h).

| Define | Default | Meaning |
| --- | --- | --- |
| `PIN_RADAR_OUT` | `3` | Radar `OUT`. Must be GPIO0–GPIO5 (deep-sleep wake capable). |
| `RD04_USE_I2C` | `0` | `1` = program the radar registers at power-up. |
| `PIN_RADAR_SDA` / `SCL` / `I2CEN` | `6` / `7` / `10` | Only used when `RD04_USE_I2C = 1`. |
| `PIN_BOOT` | `9` | BOOT button (cannot wake from deep sleep). |
| `PIN_BOOT_WAKE` | `4` | The jumpered wake pin. `-1` = no jumper installed. |
| `PIN_LED` | `8` | On-board blue LED, active LOW. |
| `EVENT_BLINK_MS` | `30` | LED flash after storing an event. `0` = off. |
| `EVENT_MERGE_GAP_S` | `0` | Merge a new pulse into the previous one if it starts within this many seconds. `0` = log every pulse. |
| `AP_SSID` | `"RD04-Logger"` | Access point name. |
| `AP_PASSWORD` | `"12345678"` | Min 8 characters, `""` = open network. |
| `AP_IDLE_TIMEOUT_S` | `300` | Close the AP and sleep after this long without requests. |
| `RD04_TX_POWER` | `5` | `0` = strongest/longest range … `7` = weakest. |
| `RD04_THRESHOLD` | `0x015A` | Detection threshold, higher = less sensitive. |
| `RD04_HOLD_MS` | `500` | How long `OUT` stays HIGH after a detection. |
| `RD04_LOCKOUT_MS` | `1000` | Blind time after `OUT` goes LOW. |

---

## HTTP API

Served only while the access point is open ([`src/webui.cpp`](src/webui.cpp)).

| Method | Path | Description |
| --- | --- | --- |
| `GET` | `/` | The log page. |
| `GET` | `/api/status` | `{"now","timeValid","boot","count","capacity","idleTimeout"}` |
| `POST` | `/api/time?ms=<epoch_ms>` | Set the clock; also records the offset for this boot session. |
| `GET` | `/api/events?limit=<n>&dl=1` | CSV, newest first. `limit=0` = everything, `dl` adds a download header. Columns: `ts,time_valid,seq,boot`. |
| `POST` | `/api/clear` | Erase the whole log. |
| `POST` | `/api/sleep` | Close the AP and go back to sleep. |

Anything else returns a `302` to the AP address, which is what makes the captive portal pop up.

Example:

```bash
curl "http://192.168.4.1/api/events?limit=0" -o events.csv
```

---

## Project layout

```
platformio.ini      build configuration (board, partitions, build flags)
partitions.csv      custom partition table with the evlog partition
images/             photos, diagrams and screenshots for this README
src/
  config.h          every pin, timing and radar parameter
  main.cpp          wake-up logic and the deep-sleep state machine
  eventlog.h/.cpp   circular event log in raw flash
  rd04.h/.cpp       optional I2C configuration of the radar
  webui.h/.cpp      access point, captive portal, HTTP API
  index_html.h      the web page, as a PROGMEM string
```

---

## Power consumption

Rough figures for the SuperMini + RD-04 pair:

| State | Current |
| --- | --- |
| Radar in pulse-power mode, ESP in deep sleep | ~110 µA (dominated by the radar) |
| Radar in continuous mode | ~16 mA |
| ESP awake logging an event | tens of mA, for a few ms |
| Access point open | ~100 mA |

The practical advice that follows from the table: keep `RD_DEBUG` off, keep `EVENT_BLINK_MS` small or
zero, and do not leave the access point open longer than you need to. Note that many ESP32-C3
SuperMini clones have a power LED or a regulator with a poor quiescent current that dwarfs all of the
above — removing that LED is the single biggest win on those boards.

---

## Troubleshooting

**The BOOT button does nothing.** The GPIO9 to GPIO4 jumper is missing or `PIN_BOOT_WAKE` is `-1`.
Use the RST button instead, or add the jumper.

**The board never wakes on motion.** `OUT` must be on GPIO0–GPIO5. Check that the radar is actually
pulsing `OUT` (a multimeter or a scope on the pin, or temporarily enable `RD_DEBUG`).

**Events pile up in bursts.** That is one radar pulse per row. Raise `EVENT_MERGE_GAP_S`, or raise
`RD04_LOCKOUT_MS` / `RD04_THRESHOLD` with `RD04_USE_I2C = 1`.

**Timestamps show "+N seconds since boot".** The clock was never synced during that boot session.
Open the page — from then on that session's events get real dates.

**The battery drains in days.** Check the board's own quiescent current first (see
[Power consumption](#power-consumption)), then confirm the radar is really in pulse-power mode.

**Upload fails.** Hold BOOT while plugging in the USB cable to force the bootloader, then upload.

---

## License

[Apache License 2.0](LICENSE).

The RD-04 register values are derived from Ai-Thinker's published reference driver and module
documentation.
