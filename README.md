# Flight Glance

A small dual-screen ADS-B glance for an ESP32: live aircraft near **Shrewsbury** on a 1.28" round GC9A01 radar and a 1.44" ST7735 detail card.

Traffic comes from the free [adsb.fi](https://opendata.adsb.fi/) Open Data API (no API key). Ground contacts are filtered out.

## Hardware

- ESP32 DevKit (WROOM / D0WD)
- DIYmalls 1.44" ST7735S 128×128 (v1171) — **3.3 V only**
- GC9A01 240×240 round — **3.3 V only**

Both displays share SPI clock and data; each has its own CS / DC / RST.

| Signal | ST7735 | GC9A01 | ESP32 |
| --- | --- | --- | --- |
| VCC / LED (BLK) | 3.3 V | 3.3 V | 3.3 V |
| GND | GND | GND | GND |
| SCK / SCL | SCK | SCL | GPIO 18 |
| MOSI / SDA | SDA | SDA | GPIO 23 |
| CS | CS | CS | GPIO 5 / **GPIO 15** |
| DC / A0 | A0 | DC | GPIO 2 / **GPIO 21** |
| RST | RESET | RST | GPIO 4 / **GPIO 22** |

## Build and flash

[PlatformIO](https://platformio.org/) CLI:

```bash
pio run -t upload
pio device monitor
```

First boot: join the **FlightGlance** Wi-Fi access point, open the portal, and save your home network. Credentials stay on the ESP32 (not in this repo).

## What it shows

- **Round screen:** north-up radar, ~80 km radius, you in the centre. Green dots are aircraft; the amber triangle is the selected one.
- **Square screen:** callsign, type, registration, speed, altitude, heading, distance, squawk. Cycles nearest-first every few seconds.

Position is hardcoded to Shrewsbury (`52.699468, -2.787509`).
