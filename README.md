# Flight Glance

Firmware for an **ESP32 DevKit** with a **1.28″ round GC9A01** radar and a **1.44″ ST7735** side card. Same ADS-B radar as [mini-radar-c3](../mini-radar-c3) (themes, range presets, sweep, runways, WiFi portal), with the OLED replaced by the larger colour side screen.

Traffic comes from the free [adsb.fi](https://opendata.adsb.fi/) Open Data API (no API key). Ground contacts are filtered out.

## Hardware

- ESP32 DevKit (WROOM / D0WD)
- DIYmalls 1.44" ST7735S 128×128 (v1171) — **3.3 V only**
- GC9A01 240×240 round — **3.3 V only**

Both displays are **3.3 V only**. They use **separate SPI buses** so the radar sweep is not stalled by the side card.

| Signal | ST7735 (HSPI) | GC9A01 (VSPI) |
| --- | --- | --- |
| VCC / LED (BLK) | 3.3 V | 3.3 V |
| GND | GND | GND |
| SCK / SCL | **GPIO 25** | **GPIO 18** |
| MOSI / SDA | **GPIO 26** | **GPIO 23** |
| CS | GPIO 5 | GPIO 15 |
| DC / A0 | GPIO 2 | GPIO 21 |
| RST | GPIO 4 | GPIO 22 |

Move only the ST7735 clock and data lines off the round display: SCK **18 → 25**, SDA **23 → 26**. CS / DC / RST stay as they are.

Onboard **BOOT** (GPIO 0): short tap cycles colour theme; hold ~1.5 s cycles range; hold ~8 s clears Wi‑Fi and opens the setup portal.

## Panel buttons

Momentary switches to **GND** (internal pull-up). Leave a pin unwired if you do not need that control.

| Button | ESP32 | Action |
| --- | --- | --- |
| Theme | GPIO **27** | Cycle colour theme |
| Range | GPIO **14** | Cycle range preset (5 → 10 → 15 → 25 km) |
| Next | GPIO **13** | Next aircraft on the side card |

```
GPIO 13 ── next  ── GND
GPIO 14 ── range ── GND
GPIO 27 ── theme ── GND
```

## What it shows

- **Round screen:** north-up radar from mini-radar-c3 — themes, range rings, sweep, runway overlay, aircraft icons, rim dots for traffic beyond the ring.
- **Side card:** place name and range, then a detail card for the selected aircraft (callsign, type, registration, speed, altitude, heading, distance). Next button skips aircraft; new pickups flash the callsign; range changes flash the radius.

Position defaults to Shrewsbury (`52.699468, -2.787509`) and can be changed in the portal.

## Wi‑Fi

First boot uses `src/secrets.h` if present (SSID/password). Otherwise join AP **`PlaneRadar-Setup`** and open **`http://plane-radar.local`** or **`http://192.168.4.1`**.

After Wi‑Fi is up, the same portal stays available on the LAN for location, range, theme, units, and runways.

```bash
cp include/secrets.h.example src/secrets.h
```

`src/secrets.h` is gitignored and must not be committed.

## Build and flash

[PlatformIO](https://platformio.org/) CLI:

```bash
.venv/bin/python -m platformio run -t upload
.venv/bin/python -m platformio device monitor
```

## Controls

| Action | Effect |
|--------|--------|
| **Theme button** (GPIO 27) | Cycle colour theme |
| **Range button** (GPIO 14) | Cycle range preset (5 → 10 → 15 → 25 km); saved to flash |
| **Next button** (GPIO 13) | Skip to the next aircraft on the side card |
| **BOOT short tap** | Cycle colour theme |
| **BOOT hold ~1.5 s** | Cycle range preset |
| **BOOT hold ~8 s** | Clear Wi‑Fi, location, and units; reboot into setup portal |

Range, miles/km, theme, grid style, sweep, and runway overlay persist in NVS (`planeradar` namespace).
