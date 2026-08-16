#include "ui/radar_range.h"

#include "config.h"

#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsRangeKey[] = "rangeIdx";
constexpr char kPrefsMilesKey[] = "useMiles";
constexpr char kPrefsRunwaysKey[] = "showRwys";
constexpr char kPrefsCardinalsKey[] = "showCard";
constexpr char kPrefsGridStyleKey[] = "gridStyl";
constexpr uint8_t kDefaultRangeIndex = 1;  // 10 km ring
constexpr uint8_t kDefaultGridStyle = 0;   // Classic
constexpr float kKmPerMile = 1.609344f;

constexpr const char* kGridStyleNames[] = {
    "Classic", "Rings", "Cross", "Spokes", "Reticle", "Minimal",
};
constexpr size_t kGridStyleCount =
    sizeof(kGridStyleNames) / sizeof(kGridStyleNames[0]);

Preferences s_prefs;
uint8_t s_range_index = kDefaultRangeIndex;
bool s_use_miles = true;
bool s_show_runways = true;
bool s_show_cardinals = true;
uint8_t s_grid_style = kDefaultGridStyle;

void saveRangeIndex() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUChar(kPrefsRangeKey, s_range_index);
  s_prefs.end();
}

void saveUseMiles() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsMilesKey, s_use_miles);
  s_prefs.end();
}

void saveShowRunways() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsRunwaysKey, s_show_runways);
  s_prefs.end();
}

void saveShowCardinals() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsCardinalsKey, s_show_cardinals);
  s_prefs.end();
}

void saveGridStyle() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUChar(kPrefsGridStyleKey, s_grid_style);
  s_prefs.end();
}

bool portalCheckboxChecked(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  // WiFiManager checkbox submits its value= attribute ("T", or "F" if we prefilled F).
  if ((value[0] == 'T' || value[0] == 't' || value[0] == 'F' || value[0] == 'f') &&
      value[1] == '\0') {
    return true;
  }
  return strcmp(value, "on") == 0;
}

}  // namespace

void rangeInit() {
  if (!s_prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  const uint8_t saved = s_prefs.getUChar(kPrefsRangeKey, kDefaultRangeIndex);
  s_range_index =
      (saved < kRangePresetCount) ? saved : kDefaultRangeIndex;
  s_use_miles = s_prefs.getBool(kPrefsMilesKey, true);
  s_show_runways = s_prefs.getBool(kPrefsRunwaysKey, true);
  s_show_cardinals = s_prefs.getBool(kPrefsCardinalsKey, true);
  const uint8_t grid = s_prefs.getUChar(kPrefsGridStyleKey, kDefaultGridStyle);
  s_grid_style = (grid < kGridStyleCount) ? grid : kDefaultGridStyle;
  s_prefs.end();
}

void rangeNext() {
  s_range_index = static_cast<uint8_t>((s_range_index + 1) % kRangePresetCount);
  saveRangeIndex();
}

const RangePreset& rangeCurrent() { return kRangePresets[s_range_index]; }

uint8_t rangeIndex() { return s_range_index; }

float fetchRadiusKm() {
  return rangeCurrent().outer_km * config::kAdsbFetchRadiusScale;
}

bool useMiles() { return s_use_miles; }

bool showRunways() { return s_show_runways; }

bool showCardinals() { return s_show_cardinals; }

uint8_t gridStyleIndex() { return s_grid_style; }

size_t gridStyleCount() { return kGridStyleCount; }

const char* gridStyleName(uint8_t index) {
  if (index >= kGridStyleCount) {
    return kGridStyleNames[kDefaultGridStyle];
  }
  return kGridStyleNames[index];
}

void saveMilesFromPortal(const char* checkbox_value) {
  s_use_miles = portalCheckboxChecked(checkbox_value);
  saveUseMiles();
  Serial.printf("Distance units: %s\n", s_use_miles ? "miles" : "km");
}

void saveRunwaysFromPortal(const char* checkbox_value) {
  s_show_runways = portalCheckboxChecked(checkbox_value);
  saveShowRunways();
  Serial.printf("Runway overlay: %s\n", s_show_runways ? "on" : "off");
}

void saveCardinalsFromPortal(const char* checkbox_value) {
  s_show_cardinals = portalCheckboxChecked(checkbox_value);
  saveShowCardinals();
  Serial.printf("Cardinal labels: %s\n", s_show_cardinals ? "on" : "off");
}

void saveGridStyleFromPortal(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return;
  }
  char* end = nullptr;
  const long idx = strtol(value, &end, 10);
  if (end == value || (end != nullptr && *end != '\0')) {
    return;
  }
  if (idx < 0 || idx >= static_cast<long>(kGridStyleCount)) {
    return;
  }
  s_grid_style = static_cast<uint8_t>(idx);
  saveGridStyle();
  Serial.printf("Grid style: %s\n", gridStyleName(s_grid_style));
}

void saveRangeIndexFromPortal(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return;
  }
  char* end = nullptr;
  const long idx = strtol(value, &end, 10);
  if (end == value || (end != nullptr && *end != '\0')) {
    return;
  }
  if (idx < 0 || idx >= static_cast<long>(kRangePresetCount)) {
    return;
  }
  s_range_index = static_cast<uint8_t>(idx);
  saveRangeIndex();
  char label[12];
  formatCurrentRing3Label(label, sizeof(label));
  Serial.printf("Radar radius preset %u (%s)\n", s_range_index, label);
}

void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles) {
  if (use_miles) {
    const int mi = static_cast<int>(lroundf(ring3_km / kKmPerMile));
    snprintf(buf, len, "%dmi", mi);
  } else {
    const int km = static_cast<int>(lroundf(ring3_km));
    snprintf(buf, len, "%dkm", km);
  }
}

void formatCurrentRing3Label(char* buf, size_t len) {
  formatRing3Label(buf, len, rangeCurrent().outer_km, s_use_miles);
}

void unitsReset() {
  s_use_miles = true;
  s_show_runways = true;
  s_show_cardinals = true;
  s_grid_style = kDefaultGridStyle;
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.remove(kPrefsMilesKey);
    s_prefs.remove(kPrefsRunwaysKey);
    s_prefs.remove(kPrefsCardinalsKey);
    s_prefs.remove(kPrefsGridStyleKey);
    s_prefs.end();
  }
}

}  // namespace ui::radar
