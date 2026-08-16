#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::radar {

/**
 * Range presets. The labelled distance is the outer ring (and the ADS-B
 * fetch radius), always stored in km.
 *
 * Recommended for ADS-B on a 1.28″ display:
 *   5 km  — pattern / very local (airfield vicinity)
 *  10 km  — default; neighborhood spotting
 *  15 km  — wider local area
 *  25 km  — metro / regional picture
 */
struct RangePreset {
  /** Same as outer_km; kept for portal / flash label helpers. */
  float ring3_km;
  /** Outer ring = fetch radius, always stored in km. */
  float outer_km;
};

constexpr RangePreset kRangePresets[] = {
    {5.0f, 5.0f},
    {10.0f, 10.0f},
    {15.0f, 15.0f},
    {25.0f, 25.0f},
};

constexpr size_t kRangePresetCount =
    sizeof(kRangePresets) / sizeof(kRangePresets[0]);

/** Load saved range and distance units from flash. Call once after boot. */
void rangeInit();
/** Cycle preset and save to flash. */
void rangeNext();
const RangePreset& rangeCurrent();
uint8_t rangeIndex();
/** ADS-B fetch radius (km): the labelled outer ring. */
float fetchRadiusKm();

bool useMiles();
bool showRunways();
bool showCardinals();
/** Grid style index (Classic / Rings / Cross / Spokes / Reticle / Minimal). */
uint8_t gridStyleIndex();
size_t gridStyleCount();
const char* gridStyleName(uint8_t index);
/** WiFi portal checkbox: "T" = miles, otherwise km. */
void saveMilesFromPortal(const char* checkbox_value);
void saveRunwaysFromPortal(const char* checkbox_value);
void saveCardinalsFromPortal(const char* checkbox_value);
void saveGridStyleFromPortal(const char* value);
/** Portal range preset index as decimal string ("0".."3"). */
void saveRangeIndexFromPortal(const char* value);
void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles);
void formatCurrentRing3Label(char* buf, size_t len);
/** Reset distance units to defaults (e.g. with WiFi credential wipe). */
void unitsReset();

}  // namespace ui::radar
