#pragma once

namespace services::location {

/** Load saved lat/lon from NVS, or use config defaults. Call once before WiFi setup. */
void init();

/** Active radar center (portal / testing location). */
double lat();
double lon();

/** Stored home coordinates (permanent base). */
double homeLat();
double homeLon();

/** Parse portal strings, validate, persist to NVS, update runtime values. */
bool saveFromStrings(const char* lat_str, const char* lon_str);

/** Switch active location to stored home and persist. */
void restoreHome();

/** Restore active location to home and clear the active override in NVS. */
void clear();

}  // namespace services::location
