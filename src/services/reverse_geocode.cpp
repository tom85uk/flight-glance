#include "services/reverse_geocode.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"

namespace services::geocode {

namespace {

constexpr size_t kPlaceLen = 32;
char s_place[kPlaceLen] = "";
double s_cached_lat = 0.0;
double s_cached_lon = 0.0;
bool s_have_cache = false;
/** True when cache holds a real place name (not a failed lookup). */
bool s_have_name = false;

bool near(double lat, double lon, double ref_lat, double ref_lon) {
  return fabs(lat - ref_lat) < 1e-3 && fabs(lon - ref_lon) < 1e-3;
}

bool coordsMatch(double lat, double lon) {
  return s_have_cache && fabs(lat - s_cached_lat) < 1e-5 &&
         fabs(lon - s_cached_lon) < 1e-5;
}

void setPlace(const char* name, double lat, double lon, bool is_real_name) {
  if (name == nullptr || name[0] == '\0') {
    s_place[0] = '\0';
    s_have_name = false;
  } else {
    strncpy(s_place, name, kPlaceLen - 1);
    s_place[kPlaceLen - 1] = '\0';
    s_have_name = is_real_name;
  }
  s_cached_lat = lat;
  s_cached_lon = lon;
  s_have_cache = true;
}

/** Offline / API-failure labels for known radar centers. */
const char* knownPlaceName(double lat, double lon) {
  if (near(lat, lon, config::kHomeRadarLat, config::kHomeRadarLon)) {
    return "Shrewsbury";
  }
  if (near(lat, lon, config::kDefaultRadarLat, config::kDefaultRadarLon)) {
    return "Manchester";
  }
  return nullptr;
}

bool looksLikeUsefulPlace(const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }
  // Skip oversized country strings and bare regions when a town exists elsewhere.
  if (strstr(name, "United Kingdom") != nullptr) {
    return false;
  }
  return true;
}

const char* pickLabel(const JsonDocument& doc) {
  // Prefer city over locality (locality is often a borough, e.g. "Centrum").
  const char* city = doc["city"] | "";
  if (looksLikeUsefulPlace(city)) {
    return city;
  }
  const char* locality = doc["locality"] | "";
  if (looksLikeUsefulPlace(locality)) {
    return locality;
  }

  // BigDataCloud puts UK towns in localityInfo.administrative with high adminLevel.
  JsonArrayConst admins = doc["localityInfo"]["administrative"].as<JsonArrayConst>();
  if (!admins.isNull()) {
    const char* best = "";
    int best_level = -1;
    for (JsonObjectConst row : admins) {
      const char* name = row["name"] | "";
      if (!looksLikeUsefulPlace(name)) {
        continue;
      }
      const int level = row["adminLevel"] | 0;
      // Prefer town/city level entries (typically 8–10 in GB).
      if (level >= 8 && level > best_level) {
        best = name;
        best_level = level;
      }
    }
    if (best[0] != '\0') {
      return best;
    }
  }

  const char* sub = doc["principalSubdivision"] | "";
  if (looksLikeUsefulPlace(sub)) {
    return sub;
  }
  return "";
}

}  // namespace

const char* placeName() { return s_place; }

void clear() {
  s_place[0] = '\0';
  s_have_cache = false;
  s_have_name = false;
}

bool refreshPlaceName(double lat, double lon) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  if (coordsMatch(lat, lon) && s_have_name) {
    return true;
  }

  char url[160];
  snprintf(url, sizeof(url),
           "https://api.bigdatacloud.net/data/reverse-geocode-client"
           "?latitude=%.6f&longitude=%.6f&localityLanguage=en",
           lat, lon);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("geocode: http.begin failed");
    const char* known = knownPlaceName(lat, lon);
    if (known != nullptr) {
      setPlace(known, lat, lon, true);
      return true;
    }
    return false;
  }
  http.setTimeout(8000);
  http.addHeader("User-Agent", "PlaneRadar/1.0");

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("geocode: HTTP %d\n", code);
    http.end();
    const char* known = knownPlaceName(lat, lon);
    if (known != nullptr) {
      setPlace(known, lat, lon, true);
      Serial.printf("geocode: fallback %s (%.6f, %.6f)\n", s_place, lat, lon);
      return true;
    }
    return false;
  }

  const String payload = http.getString();
  http.end();

  // Filter keeps heap use low on ESP32-C3 (full localityInfo blobs are huge).
  JsonDocument filter;
  filter["city"] = true;
  filter["locality"] = true;
  filter["principalSubdivision"] = true;
  filter["localityInfo"]["administrative"][0]["name"] = true;
  filter["localityInfo"]["administrative"][0]["adminLevel"] = true;

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("geocode: JSON parse error: %s\n", err.c_str());
    const char* known = knownPlaceName(lat, lon);
    if (known != nullptr) {
      setPlace(known, lat, lon, true);
      Serial.printf("geocode: fallback %s (%.6f, %.6f)\n", s_place, lat, lon);
      return true;
    }
    return false;
  }

  const char* label = pickLabel(doc);
  if (label[0] == '\0') {
    const char* known = knownPlaceName(lat, lon);
    if (known != nullptr) {
      setPlace(known, lat, lon, true);
    } else {
      // Do not cache lat/lng as a "name" — leave empty so we retry later.
      s_place[0] = '\0';
      s_have_name = false;
      s_have_cache = false;
      Serial.printf("geocode: no label for %.6f, %.6f\n", lat, lon);
      return false;
    }
  } else {
    setPlace(label, lat, lon, true);
  }

  Serial.printf("geocode: %s (%.6f, %.6f)\n", s_place, lat, lon);
  return s_place[0] != '\0';
}

}  // namespace services::geocode
