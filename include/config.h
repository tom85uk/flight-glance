#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://plane-radar.local */
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- BOOT button (ESP32 DevKit onboard BOOT, GPIO 0, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_0;
/** Long hold: cycle radar range. */
constexpr unsigned long kRangeLongHoldMs = 1500UL;
/** BOOT only: longer hold clears Wi‑Fi and opens setup portal. */
constexpr unsigned long kWifiResetHoldMs = 8000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Capacitive touch: not wired on this dual-display board ---
constexpr bool kTouchEnabled = false;
constexpr gpio_num_t kTouchPin = GPIO_NUM_32;
constexpr bool kTouchActiveHigh = true;
constexpr unsigned long kTouchTapDebounceMs = 80UL;

// --- Panel buttons (momentary to GND, internal pull-up, active LOW) ---
constexpr bool kButtonsEnabled = true;
constexpr gpio_num_t kThemeButtonPin = GPIO_NUM_27;
constexpr gpio_num_t kRangeButtonPin = GPIO_NUM_14;
constexpr gpio_num_t kNextButtonPin = GPIO_NUM_13;
constexpr unsigned long kButtonDebounceMs = 40UL;

// Round GC9A01 on VSPI (SPI3): SCK=18  MOSI=23
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_23;
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_18;

// --- Round display: GC9A01 1.28" 240×240 ---
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_22;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_15;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_21;

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;

// --- Side card: ST7735S 1.44" 128×128 on HSPI (SPI2) ---
constexpr gpio_num_t kSidePinMosi = GPIO_NUM_26;  // SDA — do not share with the round display
constexpr gpio_num_t kSidePinSclk = GPIO_NUM_25;
constexpr gpio_num_t kSidePinCs = GPIO_NUM_5;
constexpr gpio_num_t kSidePinDc = GPIO_NUM_2;
constexpr gpio_num_t kSidePinRst = GPIO_NUM_4;
constexpr int kSideWidth = 128;
constexpr int kSideHeight = 128;
/** 2 = 180° so the card is upright in the case. */
constexpr uint8_t kSideRotation = 2;
/** 1.44" greentab: use 1 with rotation 2 (3 leaves a noisy strip at the bottom). */
constexpr int kSideOffsetX = 2;
constexpr int kSideOffsetY = 1;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
constexpr uint32_t kSideSpiWriteHz = 27000000;
// GC9A01 modules often need invert + BGR for correct black/green output
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;

// --- Alert LED: not wired (GPIO 2 is the ST7735 DC pin) ---
constexpr bool kAlertLedEnabled = false;
constexpr gpio_num_t kAlertLedPin = GPIO_NUM_12;
constexpr unsigned long kAlertLedFlashMs = 120;
constexpr unsigned long kAlertLedFlashGapMs = 100;
constexpr unsigned long kOledRangeFlashMs = 1400;
constexpr unsigned long kOledAircraftFlashMs = 1600;

// --- Radar center ---
/** Permanent home — Shrewsbury (restored when "Use home location" is selected). */
constexpr double kHomeRadarLat = 52.699468;
constexpr double kHomeRadarLon = -2.787509;
/** Active default until the portal overrides it. */
constexpr double kDefaultRadarLat = 52.699468;
constexpr double kDefaultRadarLon = -2.787509;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 3000;
constexpr float kAdsbFetchRadiusScale = 1.0f;
constexpr bool kAdsbShowGroundAircraft = false;

constexpr unsigned long kRadarSweepPeriodMs = 2800;
/** Sweep task period. Full-frame composite is one SPI push; include that time. */
constexpr unsigned long kRadarSweepFrameMs = 33;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
