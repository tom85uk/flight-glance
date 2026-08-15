#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <ArduinoJson.h>

#include <cstring>

#include "config.h"

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
/** Was 200ms — too aggressive; work Wi‑Fi / TLS handshakes often need seconds. */
constexpr int kConnectAttemptMs = 8000;
constexpr unsigned long kRequestTimeoutMs = 15000;

Aircraft s_aircraft[kMaxAircraft];
Aircraft s_build[kMaxAircraft];
size_t s_aircraft_count = 0;
PollFn s_poll_fn = nullptr;
SemaphoreHandle_t s_mu = nullptr;
volatile bool s_updated = false;

void ensureMutex() {
  if (s_mu == nullptr) {
    s_mu = xSemaphoreCreateMutex();
  }
}

void publishAircraft(Aircraft* src, size_t n) {
  ensureMutex();
  xSemaphoreTake(s_mu, portMAX_DELAY);
  if (n > 0 && src != s_aircraft) {
    memcpy(s_aircraft, src, n * sizeof(Aircraft));
  }
  s_aircraft_count = n;
  s_updated = true;
  xSemaphoreGive(s_mu);
}

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

int performGetWithPoll(HTTPClient& http) {
  http.setConnectTimeout(kConnectAttemptMs);
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    pollNetwork();
    const int code = http.GET();
    if (code > 0) {
      return code;
    }
    if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
        code != HTTPC_ERROR_NOT_CONNECTED) {
      return code;
    }
    delay(5);
  }
  return HTTPC_ERROR_READ_TIMEOUT;
}

bool readResponseBodyWithPoll(HTTPClient& http, String& payload) {
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    return false;
  }

  const int content_length = http.getSize();
  if (content_length > 0) {
    payload.reserve(static_cast<unsigned>(content_length + 1));
  }

  uint8_t buffer[512];
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    pollNetwork();
    const int available = stream->available();
    if (available > 0) {
      const int to_read =
          available > static_cast<int>(sizeof(buffer)) ? static_cast<int>(sizeof(buffer))
                                                       : available;
      const int read_bytes = stream->readBytes(buffer, to_read);
      if (read_bytes > 0) {
        payload.concat(reinterpret_cast<const char*>(buffer),
                       static_cast<unsigned>(read_bytes));
      }
    }
    if (content_length > 0 &&
        static_cast<int>(payload.length()) >= content_length) {
      break;
    }
    if (!http.connected() && stream->available() <= 0) {
      break;
    }
    delay(1);
  }

  return payload.length() > 0;
}

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "tas", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "ias", &v)) {
    return v;
  }
  return 0.0f;
}

bool isOnGround(const JsonObject& plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out,
                           size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || !obj[key].is<const char*>()) {
    return;
  }
  const char* s = obj[key].as<const char*>();
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') {
    --n;
  }
  memcpy(out, s, n);
  out[n] = '\0';
}

void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }

  if (plane["alt_baro"].is<const char*>()) {
    const char* s = plane["alt_baro"].as<const char*>();
    if (strcmp(s, "ground") == 0) {
      strncpy(out, "GND", out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }
  }

  float alt = 0.0f;
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    snprintf(out, out_len, "%d ft", static_cast<int>(lroundf(alt)));
  }
}

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  copyJsonStringTrimmed(plane, "hex", ac->hex, sizeof(ac->hex));
  copyJsonStringTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    copyJsonStringTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  copyJsonStringTrimmed(plane, "desc", ac->desc, sizeof(ac->desc));
  copyJsonStringTrimmed(plane, "r", ac->reg, sizeof(ac->reg));
  if (plane["squawk"].is<const char*>()) {
    copyJsonStringTrimmed(plane, "squawk", ac->squawk, sizeof(ac->squawk));
  } else if (plane["squawk"].is<int>()) {
    snprintf(ac->squawk, sizeof(ac->squawk), "%04d", plane["squawk"].as<int>());
  } else {
    ac->squawk[0] = '\0';
  }
  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

void lockAircraft() {
  ensureMutex();
  xSemaphoreTake(s_mu, portMAX_DELAY);
}

void unlockAircraft() { xSemaphoreGive(s_mu); }

bool consumeUpdated() {
  ensureMutex();
  xSemaphoreTake(s_mu, portMAX_DELAY);
  const bool u = s_updated;
  s_updated = false;
  xSemaphoreGive(s_mu);
  return u;
}

size_t aircraftCount() { return s_aircraft_count; }

const Aircraft* aircraftList() { return s_aircraft; }

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  const float dist_nm = kmToNauticalMiles(fetch_radius_km);

  String url = kApiBase;
  url += String(center_lat, 6);
  url += "/lon/";
  url += String(center_lon, 6);
  url += "/dist/";
  url += String(dist_nm, 1);

  static unsigned long s_last_diag_ms = 0;
  const unsigned long now = millis();
  const bool log_diag =
      (s_last_diag_ms == 0) || (now - s_last_diag_ms >= 30000);
  if (log_diag) {
    s_last_diag_ms = now;
    Serial.printf("adsb: fetch %.6f,%.6f r=%.1f nm (%.1f km)\n", center_lat,
                  center_lon, dist_nm, fetch_radius_km);
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(kRequestTimeoutMs / 1000);

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("adsb: http.begin failed");
    return false;
  }

  http.setTimeout(kRequestTimeoutMs);
  const int code = performGetWithPoll(http);
  if (code != HTTP_CODE_OK) {
    Serial.printf("adsb: HTTP %d (TLS/WiFi/firewall?)\n", code);
    http.end();
    return false;
  }

  String payload;
  if (!readResponseBodyWithPoll(http, payload)) {
    Serial.println("adsb: empty response");
    http.end();
    return false;
  }
  http.end();

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("adsb: JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonArray ac = doc["ac"].as<JsonArray>();
  if (ac.isNull()) {
    publishAircraft(s_build, 0);
    if (log_diag) {
      Serial.println("adsb: 0 aircraft (API ok, none in radius)");
    } else {
      Serial.printf("adsb: %u aircraft\n", 0u);
    }
    return true;
  }

  size_t n = 0;
  size_t skipped_ground = 0;
  for (JsonObject plane : ac) {
    if (n >= kMaxAircraft) {
      break;
    }
    if (!plane["lat"].is<float>() || !plane["lon"].is<float>()) {
      continue;
    }
    if (isOnGround(plane) && !config::kAdsbShowGroundAircraft) {
      ++skipped_ground;
      continue;
    }

    s_build[n].lat = plane["lat"].as<float>();
    s_build[n].lon = plane["lon"].as<float>();
    s_build[n].nose_deg = pickNoseHeading(plane);
    s_build[n].track_deg = pickTrackHeading(plane);
    s_build[n].gs_knots = pickGroundSpeed(plane);
    fillTagFields(&s_build[n], plane);
    ++n;
  }

  publishAircraft(s_build, n);
  if (skipped_ground > 0 && n == 0) {
    Serial.printf("adsb: 0 airborne (%u ground filtered)\n",
                  static_cast<unsigned>(skipped_ground));
  } else {
    Serial.printf("adsb: %u aircraft\n", static_cast<unsigned>(n));
  }
  return true;
}

}  // namespace services::adsb
