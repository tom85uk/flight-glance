#include "services/radar_location.h"

#include <Preferences.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "services/reverse_geocode.h"

namespace services::location {

namespace {

constexpr char kPrefsNamespace[] = "radar";
constexpr char kKeyLat[] = "lat";
constexpr char kKeyLon[] = "lon";
constexpr char kKeyHomeLat[] = "homeLat";
constexpr char kKeyHomeLon[] = "homeLon";
constexpr char kKeyManTest[] = "manTest1";

double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;
double s_home_lat = config::kHomeRadarLat;
double s_home_lon = config::kHomeRadarLon;

bool parseCoord(const char* text, double* out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const double v = strtod(text, &end);
  if (end == text || (end != nullptr && *end != '\0')) {
    return false;
  }
  *out = v;
  return true;
}

bool validLatLon(double lat, double lon) {
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

bool nearCoords(double lat, double lon, double ref_lat, double ref_lon) {
  return fabs(lat - ref_lat) < 1e-4 && fabs(lon - ref_lon) < 1e-4;
}

void persistActive(double lat, double lon) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putDouble(kKeyLat, lat);
  prefs.putDouble(kKeyLon, lon);
  prefs.end();
  s_lat = lat;
  s_lon = lon;
}

void persistHome(double lat, double lon) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putDouble(kKeyHomeLat, lat);
  prefs.putDouble(kKeyHomeLon, lon);
  prefs.end();
  s_home_lat = lat;
  s_home_lon = lon;
}

}  // namespace

void init() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);

  // Home is always Shrewsbury (config::kHomeRadar*). Keep NVS in sync.
  s_home_lat = config::kHomeRadarLat;
  s_home_lon = config::kHomeRadarLon;
  prefs.putDouble(kKeyHomeLat, s_home_lat);
  prefs.putDouble(kKeyHomeLon, s_home_lon);

  // One-shot: move active center to Manchester for ADS-B testing.
  if (!prefs.getBool(kKeyManTest, false)) {
    prefs.putDouble(kKeyLat, config::kDefaultRadarLat);
    prefs.putDouble(kKeyLon, config::kDefaultRadarLon);
    prefs.putBool(kKeyManTest, true);
    s_lat = config::kDefaultRadarLat;
    s_lon = config::kDefaultRadarLon;
    services::geocode::clear();
    Serial.printf("Radar location set to Manchester test: %.6f, %.6f\n", s_lat,
                  s_lon);
    prefs.end();
    return;
  }

  if (prefs.isKey(kKeyLat) && prefs.isKey(kKeyLon)) {
    const double lat = prefs.getDouble(kKeyLat, config::kDefaultRadarLat);
    const double lon = prefs.getDouble(kKeyLon, config::kDefaultRadarLon);
    // Upstream Plane Radar default was Amsterdam — migrate away.
    constexpr double kLegacyAmsterdamLat = 52.3676;
    constexpr double kLegacyAmsterdamLon = 4.9041;
    if (nearCoords(lat, lon, kLegacyAmsterdamLat, kLegacyAmsterdamLon)) {
      prefs.putDouble(kKeyLat, config::kDefaultRadarLat);
      prefs.putDouble(kKeyLon, config::kDefaultRadarLon);
      s_lat = config::kDefaultRadarLat;
      s_lon = config::kDefaultRadarLon;
      services::geocode::clear();
      Serial.println("Radar location: migrated Amsterdam default -> Manchester");
    } else if (validLatLon(lat, lon)) {
      s_lat = lat;
      s_lon = lon;
    }
  } else {
    s_lat = config::kDefaultRadarLat;
    s_lon = config::kDefaultRadarLon;
  }
  prefs.end();
}

double lat() { return s_lat; }

double lon() { return s_lon; }

double homeLat() { return s_home_lat; }

double homeLon() { return s_home_lon; }

bool saveFromStrings(const char* lat_str, const char* lon_str) {
  double lat = 0.0;
  double lon = 0.0;
  if (!parseCoord(lat_str, &lat) || !parseCoord(lon_str, &lon)) {
    return false;
  }
  if (!validLatLon(lat, lon)) {
    return false;
  }
  persistActive(lat, lon);
  services::geocode::clear();
  Serial.printf("Radar location saved: %.6f, %.6f\n", lat, lon);
  return true;
}

void restoreHome() {
  persistHome(config::kHomeRadarLat, config::kHomeRadarLon);
  persistActive(config::kHomeRadarLat, config::kHomeRadarLon);
  services::geocode::clear();
  Serial.printf("Radar location set to Shrewsbury home: %.6f, %.6f\n", s_lat,
                s_lon);
}

void clear() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.remove(kKeyLat);
  prefs.remove(kKeyLon);
  prefs.end();
  s_home_lat = config::kHomeRadarLat;
  s_home_lon = config::kHomeRadarLon;
  s_lat = s_home_lat;
  s_lon = s_home_lon;
  services::geocode::clear();
  Serial.printf("Radar location restored to Shrewsbury home: %.6f, %.6f\n",
                s_lat, s_lon);
}

}  // namespace services::location
