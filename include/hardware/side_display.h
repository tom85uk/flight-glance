#pragma once

/** Init the 1.44″ ST7735 side card. Safe if the panel is missing. */
bool sideInit();

bool sideReady();

/** Status while connecting / in portal / offline. */
void sideShowStatus(const char* line1, const char* line2 = "");

/** Refresh HUD + selected aircraft from current ADS-B + range state. */
void sideShowRadarInfo();

/** Brief centered range readout; call sidePoll() to restore. */
void sideShowRangeFlash();

/** Brief theme name in the new palette; call sidePoll() to restore. */
void sideShowThemeFlash();

/** Brief big tag for a newly detected aircraft (callsign + type). */
void sideShowAircraftFlash(const char* callsign, const char* type = nullptr);

/** Ends a banner flash and restores the HUD. */
void sidePoll();

/** Skip to the next aircraft on the side card. */
void sideAdvanceCard();

/** ICAO hex of the aircraft on the side card, or empty if none. */
const char* sideSelectedHex();
