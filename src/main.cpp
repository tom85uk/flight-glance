#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_GC9A01A.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

// Flight Glance — dual-screen ADS-B glance (ST7735 card + GC9A01 radar)
// Shared SPI: SCK=18  MOSI=23
#define TFT_CS 5
#define TFT_DC 2
#define TFT_RST 4
#define RND_CS 15
#define RND_DC 21
#define RND_RST 22

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
Adafruit_GC9A01A roundTft(RND_CS, RND_DC, RND_RST);

const uint16_t COL_BG = 0x0000;
const uint16_t COL_PANEL = 0x10A2;
const uint16_t COL_GREEN = 0x07E0;
const uint16_t COL_AMBER = 0xFE00;
const uint16_t COL_CYAN = 0x07FF;
const uint16_t COL_DIM = 0x8410;
const uint16_t COL_WHITE = 0xFFFF;
const uint16_t COL_LINE = 0x3D08;
const uint16_t COL_DOT = 0x07E0;

// Shrewsbury (same home as mini-radar)
constexpr double kHomeLat = 52.699468;
constexpr double kHomeLon = -2.787509;
constexpr float kRangeKm = 80.0f;
constexpr float kFetchNm = 50.0f;  // ~93 km, a bit past the screen edge
constexpr unsigned long kFetchMs = 5000;
constexpr unsigned long kCycleMs = 4000;
constexpr size_t kMaxAircraft = 24;

struct Aircraft {
  float lat;
  float lon;
  float dist_km;
  float brg_deg;
  uint16_t speed_kt;
  uint16_t alt_ft;
  uint16_t hdg;
  bool ground;
  char callsign[9];
  char type[8];
  char reg[10];
  char squawk[6];
};

Aircraft gFleet[kMaxAircraft];
size_t gCount = 0;
int gIndex = 0;
unsigned long gLastFetch = 0;
unsigned long gLastCycle = 0;
bool gWifiOk = false;

void polar(int cx, int cy, float deg, int r, int *x, int *y) {
  const float rad = (deg - 90.0f) * (float)M_PI / 180.0f;
  *x = cx + (int)(cosf(rad) * r);
  *y = cy + (int)(sinf(rad) * r);
}

void statusBoth(const char *a, const char *b = nullptr) {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_AMBER);
  tft.setTextSize(1);
  tft.setCursor(6, 20);
  tft.println(a);
  if (b) {
    tft.setTextColor(COL_CYAN);
    tft.setCursor(6, 40);
    tft.println(b);
  }
  roundTft.fillScreen(COL_BG);
  roundTft.setTextColor(COL_AMBER);
  roundTft.setTextSize(1);
  roundTft.setCursor(50, 110);
  roundTft.println(a);
}

void trimCopy(const char *src, char *dst, size_t dst_len) {
  dst[0] = '\0';
  if (!src || dst_len == 0) {
    return;
  }
  while (*src == ' ') {
    ++src;
  }
  size_t n = strnlen(src, dst_len - 1);
  while (n > 0 && src[n - 1] == ' ') {
    --n;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

void latlonToKm(float lat, float lon, float *east_km, float *north_km) {
  *north_km = (lat - (float)kHomeLat) * 111.32f;
  *east_km = (lon - (float)kHomeLon) * 111.32f *
             cosf((float)kHomeLat * (float)M_PI / 180.0f);
}

void kmToPixel(float east_km, float north_km, int *x, int *y) {
  const float scale = 100.0f / kRangeKm;
  *x = 120 + (int)(east_km * scale);
  *y = 120 - (int)(north_km * scale);
}

void drawLabel(int x, int y, const char *k, const char *v, uint16_t vc) {
  tft.setTextSize(1);
  tft.setTextColor(COL_DIM);
  tft.setCursor(x, y);
  tft.print(k);
  tft.setTextColor(vc);
  tft.setCursor(x + 28, y);
  tft.print(v);
}

void showAircraftCard(const Aircraft &ac, int index, size_t total) {
  tft.fillScreen(COL_BG);
  tft.fillRect(0, 0, 128, 28, COL_PANEL);
  tft.drawFastHLine(0, 28, 128, COL_LINE);

  tft.setTextSize(2);
  tft.setTextColor(COL_AMBER);
  tft.setCursor(4, 6);
  tft.print(ac.callsign[0] ? ac.callsign : ac.reg);

  tft.setTextSize(1);
  tft.setTextColor(COL_CYAN);
  tft.setCursor(4, 36);
  tft.print(ac.type[0] ? ac.type : "----");
  tft.setTextColor(COL_DIM);
  tft.print("  ");
  tft.print(ac.reg);

  char buf[20];
  snprintf(buf, sizeof(buf), "%d kt", ac.speed_kt);
  drawLabel(4, 52, "SPD", buf, COL_WHITE);

  if (ac.ground) {
    snprintf(buf, sizeof(buf), "GND");
  } else if (ac.alt_ft >= 10000) {
    snprintf(buf, sizeof(buf), "FL%03d", ac.alt_ft / 100);
  } else {
    snprintf(buf, sizeof(buf), "%d ft", ac.alt_ft);
  }
  drawLabel(4, 66, "ALT", buf, COL_WHITE);

  snprintf(buf, sizeof(buf), "%03d", ac.hdg);
  drawLabel(4, 80, "HDG", buf, COL_WHITE);

  snprintf(buf, sizeof(buf), "%.0f km", ac.dist_km);
  drawLabel(4, 94, "DST", buf, COL_CYAN);

  tft.setTextColor(COL_GREEN);
  tft.setCursor(4, 112);
  tft.print(ac.squawk[0] ? ac.squawk : "----");
  tft.setTextColor(COL_DIM);
  tft.setCursor(70, 112);
  tft.print(index + 1);
  tft.print("/");
  tft.print((int)total);
}

void showRadar(int selected) {
  const int cx = 120;
  const int cy = 120;
  roundTft.fillScreen(COL_BG);
  roundTft.drawCircle(cx, cy, 112, COL_LINE);
  roundTft.drawCircle(cx, cy, 100, COL_GREEN);
  roundTft.drawCircle(cx, cy, 50, COL_LINE);
  roundTft.fillCircle(cx, cy, 3, COL_AMBER);

  roundTft.setTextSize(1);
  roundTft.setTextColor(COL_AMBER);
  roundTft.setCursor(cx - 3, 8);
  roundTft.print("N");
  roundTft.setCursor(216, cy - 3);
  roundTft.print("E");
  roundTft.setCursor(cx - 3, 226);
  roundTft.print("S");
  roundTft.setCursor(8, cy - 3);
  roundTft.print("W");

  for (size_t i = 0; i < gCount; ++i) {
    float east, north;
    latlonToKm(gFleet[i].lat, gFleet[i].lon, &east, &north);
    int x, y;
    kmToPixel(east, north, &x, &y);
    if (x < 8 || x > 232 || y < 8 || y > 232) {
      continue;
    }
    const bool sel = ((int)i == selected);
    const uint16_t col = sel ? COL_AMBER : COL_DOT;
    if (sel) {
      int nx, ny, lx, ly, rx, ry;
      polar(x, y, gFleet[i].hdg, 10, &nx, &ny);
      polar(x, y, gFleet[i].hdg + 150, 5, &lx, &ly);
      polar(x, y, gFleet[i].hdg - 150, 5, &rx, &ry);
      roundTft.fillTriangle(nx, ny, lx, ly, rx, ry, col);
      roundTft.drawCircle(x, y, 8, COL_AMBER);
    } else {
      roundTft.fillCircle(x, y, 2, col);
    }
  }

  roundTft.setTextColor(COL_DIM);
  roundTft.setCursor(78, 200);
  roundTft.print((int)kRangeKm);
  roundTft.print("km ");
  roundTft.print((int)gCount);
  roundTft.print("ac");
}

void showEmpty() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_AMBER);
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.println("No traffic");
  tft.setTextColor(COL_DIM);
  tft.setCursor(10, 60);
  tft.println("Shrewsbury");
  tft.setCursor(10, 76);
  tft.print((int)kRangeKm);
  tft.println(" km radius");
  roundTft.fillScreen(COL_BG);
  roundTft.drawCircle(120, 120, 100, COL_GREEN);
  roundTft.fillCircle(120, 120, 3, COL_AMBER);
  roundTft.setTextColor(COL_DIM);
  roundTft.setCursor(70, 112);
  roundTft.print("NO TRAFFIC");
}

void showCurrent() {
  if (gCount == 0) {
    showEmpty();
    return;
  }
  if (gIndex >= (int)gCount) {
    gIndex = 0;
  }
  showAircraftCard(gFleet[gIndex], gIndex, gCount);
  showRadar(gIndex);
}

void sortByDistance() {
  for (size_t i = 0; i + 1 < gCount; ++i) {
    for (size_t j = i + 1; j < gCount; ++j) {
      if (gFleet[j].dist_km < gFleet[i].dist_km) {
        Aircraft tmp = gFleet[i];
        gFleet[i] = gFleet[j];
        gFleet[j] = tmp;
      }
    }
  }
}

bool fetchAircraft() {
  String url = "https://opendata.adsb.fi/api/v3/lat/";
  url += String(kHomeLat, 6);
  url += "/lon/";
  url += String(kHomeLon, 6);
  url += "/dist/";
  url += String(kFetchNm, 1);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(12);

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("adsb: begin failed");
    return false;
  }
  http.setTimeout(12000);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("adsb: HTTP %d\n", code);
    http.end();
    return false;
  }

  JsonDocument filter;
  filter["ac"][0]["lat"] = true;
  filter["ac"][0]["lon"] = true;
  filter["ac"][0]["flight"] = true;
  filter["ac"][0]["hex"] = true;
  filter["ac"][0]["t"] = true;
  filter["ac"][0]["r"] = true;
  filter["ac"][0]["gs"] = true;
  filter["ac"][0]["track"] = true;
  filter["ac"][0]["true_heading"] = true;
  filter["ac"][0]["alt_baro"] = true;
  filter["ac"][0]["squawk"] = true;

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("adsb: json %s\n", err.c_str());
    return false;
  }

  JsonArray ac = doc["ac"].as<JsonArray>();
  size_t n = 0;
  if (!ac.isNull()) {
    for (JsonObject plane : ac) {
      if (n >= kMaxAircraft) {
        break;
      }
      if (!plane["lat"].is<float>() || !plane["lon"].is<float>()) {
        continue;
      }
      if (plane["alt_baro"].is<const char*>() &&
          strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0) {
        continue;
      }

      Aircraft &a = gFleet[n];
      a.lat = plane["lat"].as<float>();
      a.lon = plane["lon"].as<float>();
      float east, north;
      latlonToKm(a.lat, a.lon, &east, &north);
      a.dist_km = sqrtf(east * east + north * north);
      a.brg_deg = atan2f(east, north) * 180.0f / (float)M_PI;
      if (a.brg_deg < 0) {
        a.brg_deg += 360.0f;
      }
      a.speed_kt = (uint16_t)lroundf(plane["gs"] | 0.0f);
      float alt = 0;
      if (plane["alt_baro"].is<float>() || plane["alt_baro"].is<int>()) {
        alt = plane["alt_baro"].as<float>();
      }
      a.alt_ft = (uint16_t)constrain((int)lroundf(alt), 0, 65535);
      a.ground = false;
      float hdg = 0.0f;
      if (plane["true_heading"].is<float>() || plane["true_heading"].is<int>()) {
        hdg = plane["true_heading"].as<float>();
      } else if (plane["track"].is<float>() || plane["track"].is<int>()) {
        hdg = plane["track"].as<float>();
      }
      a.hdg = ((int)lroundf(hdg) + 360) % 360;

      const char *flight = plane["flight"] | "";
      trimCopy(flight, a.callsign, sizeof(a.callsign));
      if (a.callsign[0] == '\0') {
        trimCopy(plane["hex"] | "", a.callsign, sizeof(a.callsign));
      }
      trimCopy(plane["t"] | "", a.type, sizeof(a.type));
      trimCopy(plane["r"] | "", a.reg, sizeof(a.reg));
      if (plane["squawk"].is<const char*>()) {
        trimCopy(plane["squawk"].as<const char*>(), a.squawk, sizeof(a.squawk));
      } else if (plane["squawk"].is<int>()) {
        snprintf(a.squawk, sizeof(a.squawk), "%04d", plane["squawk"].as<int>());
      } else {
        a.squawk[0] = '\0';
      }
      ++n;
    }
  }

  gCount = n;
  sortByDistance();
  Serial.printf("adsb: %u aircraft near Shrewsbury\n", (unsigned)n);
  return true;
}

void configModeCallback(WiFiManager *wm) {
  statusBoth("WiFi setup", wm->getConfigPortalSSID().c_str());
  tft.setCursor(6, 70);
  tft.setTextColor(COL_WHITE);
  tft.println(WiFi.softAPIP());
}

bool connectWifi() {
  statusBoth("WiFi...", "FlightGlance");
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setHostname("flight-glance");
  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(180);
  const bool ok = wm.autoConnect("FlightGlance");
  if (!ok) {
    statusBoth("WiFi failed");
  }
  return ok;
}

void setup() {
  Serial.begin(115200);
  pinMode(TFT_CS, OUTPUT);
  pinMode(RND_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(RND_CS, HIGH);

  tft.initR(INITR_144GREENTAB);
  tft.setRotation(0);
  roundTft.begin();
  roundTft.setRotation(0);
  roundTft.invertDisplay(true);

  gWifiOk = connectWifi();
  if (gWifiOk) {
    statusBoth("Fetching...", WiFi.SSID().c_str());
    fetchAircraft();
    showCurrent();
    gLastFetch = millis();
    gLastCycle = millis();
  }
}

void loop() {
  if (!gWifiOk) {
    delay(1000);
    return;
  }

  const unsigned long now = millis();
  if (now - gLastFetch >= kFetchMs) {
    gLastFetch = now;
    fetchAircraft();
    showCurrent();
  }
  if (gCount > 0 && now - gLastCycle >= kCycleMs) {
    gLastCycle = now;
    gIndex = (gIndex + 1) % (int)gCount;
    showCurrent();
  }
  delay(20);
}
