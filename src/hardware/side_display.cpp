#include "hardware/side_display.h"

#include <Arduino.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/reverse_geocode.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace {

bool s_ready = false;
unsigned long s_banner_until_ms = 0;
bool s_banner_active = false;
int s_card_index = 0;
unsigned long s_last_cycle_ms = 0;
bool s_hud_active = false;

lgfx::LovyanGFX* s_g = &sideTft;

constexpr float kKmPerDeg = 111.32f;

/** Pack theme RGB for the ST7735. Do not reuse the GC9A01 kColor* values —
 *  those are R/B-swapped for the round panel. */
uint16_t sideRgb(const ui::radar::ThemeRgb& c) {
  return sideTft.color565(c.r, c.g, c.b);
}

uint16_t colBg() { return sideRgb(ui::radar::themeCurrent().bg); }
uint16_t colFg() { return sideRgb(ui::radar::themeCurrent().label); }
uint16_t colAccent() { return sideRgb(ui::radar::themeCurrent().aircraft); }
uint16_t colDim() { return sideRgb(ui::radar::themeCurrent().grid); }
uint16_t colType() { return sideRgb(ui::radar::themeCurrent().tag_type); }
uint16_t colAlt() { return sideRgb(ui::radar::themeCurrent().tag_alt); }

float distanceKm(float lat, float lon) {
  const float north = (lat - static_cast<float>(services::location::lat())) * kKmPerDeg;
  const float east = (lon - static_cast<float>(services::location::lon())) * kKmPerDeg *
                     cosf(static_cast<float>(services::location::lat()) *
                          static_cast<float>(M_PI) / 180.0f);
  return sqrtf(east * east + north * north);
}

void startBanner(unsigned long duration_ms) {
  s_banner_active = true;
  s_banner_until_ms = millis() + duration_ms;
}

/** Direct to the panel — HSPI is independent of the radar, no RAM sprite needed. */
void flushSide() {}

void drawCentered(const char* text, int y, uint16_t color, int size) {
  s_g->setTextSize(size);
  s_g->setTextColor(color, colBg());
  s_g->setTextDatum(textdatum_t::top_center);
  s_g->drawString(text, config::kSideWidth / 2, y);
}

void drawTwoLines(const char* line1, const char* line2) {
  if (!s_ready) {
    return;
  }
  s_g->fillScreen(colBg());
  s_g->setTextDatum(textdatum_t::top_left);
  s_g->setTextSize(1);
  s_g->setTextColor(colAccent(), colBg());
  s_g->setCursor(6, 20);
  s_g->print(line1 != nullptr ? line1 : "");
  if (line2 != nullptr && line2[0] != '\0') {
    s_g->setTextColor(colType(), colBg());
    s_g->setCursor(6, 40);
    s_g->print(line2);
  }
  flushSide();
}

void drawLabel(int x, int y, const char* k, const char* v, uint16_t vc) {
  s_g->setTextSize(1);
  s_g->setTextColor(colDim(), colBg());
  s_g->setCursor(x, y);
  s_g->print(k);
  s_g->setTextColor(vc, colBg());
  s_g->setCursor(x + 28, y);
  s_g->print(v);
}

void drawAircraftCard(const services::adsb::Aircraft& ac, int index, size_t total,
                      const char* place, const char* range) {
  s_g->fillScreen(colBg());
  s_g->fillRect(0, 0, config::kSideWidth, 16, colDim());

  s_g->setTextSize(1);
  s_g->setTextColor(colFg(), colDim());
  s_g->setCursor(2, 4);
  if (place != nullptr && place[0] != '\0') {
    char pbuf[18];
    snprintf(pbuf, sizeof(pbuf), "%.17s", place);
    s_g->print(pbuf);
  }
  if (range != nullptr && range[0] != '\0') {
    s_g->setTextDatum(textdatum_t::top_right);
    s_g->drawString(range, config::kSideWidth - 2, 4);
    s_g->setTextDatum(textdatum_t::top_left);
  }

  const char* title = ac.callsign[0] ? ac.callsign : ac.hex;
  s_g->setTextSize(2);
  s_g->setTextColor(colAccent(), colBg());
  s_g->setCursor(4, 22);
  s_g->print(title[0] ? title : "----");

  s_g->setTextSize(1);
  s_g->setTextColor(colType(), colBg());
  s_g->setCursor(4, 42);
  const char* type_label = services::adsb::typeLabel(ac);
  s_g->print(type_label[0] ? type_label : "----");

  int stats_y = 58;
  if (ac.reg[0] != '\0' && ac.desc[0] == '\0') {
    s_g->setTextColor(colDim(), colBg());
    s_g->print("  ");
    s_g->print(ac.reg);
  } else if (ac.reg[0] != '\0') {
    s_g->setTextColor(colDim(), colBg());
    s_g->setCursor(4, 52);
    s_g->print(ac.reg);
    stats_y = 64;
  }

  char buf[20];
  snprintf(buf, sizeof(buf), "%d kt", static_cast<int>(lroundf(ac.gs_knots)));
  drawLabel(4, stats_y, "SPD", buf, colFg());
  drawLabel(4, stats_y + 12, "ALT", ac.alt[0] ? ac.alt : "----", colAlt());

  snprintf(buf, sizeof(buf), "%03d",
           (static_cast<int>(lroundf(ac.nose_deg)) + 360) % 360);
  drawLabel(4, stats_y + 24, "HDG", buf, colFg());

  const float dist = distanceKm(ac.lat, ac.lon);
  if (ui::radar::useMiles()) {
    snprintf(buf, sizeof(buf), "%.1f mi", dist * 0.621371f);
  } else {
    snprintf(buf, sizeof(buf), "%.1f km", dist);
  }
  drawLabel(4, stats_y + 36, "DST", buf, colType());

  snprintf(buf, sizeof(buf), "%d/%u", index + 1, static_cast<unsigned>(total));
  s_g->setTextColor(colDim(), colBg());
  s_g->setTextDatum(textdatum_t::top_right);
  s_g->drawString(buf, config::kSideWidth - 4, 112);
  s_g->setTextDatum(textdatum_t::top_left);
  s_g->fillRect(0, config::kSideHeight - 4, config::kSideWidth, 4, colBg());
}

void drawEmptyHud(const char* place, const char* range) {
  s_g->fillScreen(colBg());
  s_g->setTextSize(1);
  s_g->setTextColor(colAccent(), colBg());
  s_g->setCursor(10, 28);
  s_g->print("Clear skies");
  s_g->setTextColor(colDim(), colBg());
  s_g->setCursor(10, 48);
  if (place != nullptr && place[0] != '\0') {
    s_g->print(place);
  }
  s_g->setCursor(10, 64);
  if (range != nullptr && range[0] != '\0') {
    s_g->print(range);
  }
}

void placeAndRange(char* place_buf, size_t place_len, char* range_buf,
                   size_t range_len) {
  const char* place = services::geocode::placeName();
  if (place != nullptr && place[0] != '\0') {
    snprintf(place_buf, place_len, "%s", place);
  } else {
    snprintf(place_buf, place_len, "%.3f,%.3f", services::location::lat(),
             services::location::lon());
  }
  ui::radar::formatCurrentRing3Label(range_buf, range_len);
}

void paintRadarHud() {
  if (!s_ready) {
    return;
  }

  char place_buf[22];
  char range_label[12];
  placeAndRange(place_buf, sizeof(place_buf), range_label, sizeof(range_label));

  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();

  if (n == 0 || planes == nullptr) {
    s_card_index = 0;
    drawEmptyHud(place_buf, range_label);
  } else {
    if (s_card_index >= static_cast<int>(n)) {
      s_card_index = 0;
    }
    drawAircraftCard(planes[s_card_index], s_card_index, n, place_buf,
                     range_label);
  }
  flushSide();
}

}  // namespace

bool sideInit() {
  s_g = &sideTft;
  Serial.println("Side: ST7735 128x128 (direct HSPI)");
  s_ready = true;
  s_banner_active = false;
  s_hud_active = false;
  s_card_index = 0;
  sideShowStatus("Flight Glance", "side card");
  return true;
}

bool sideReady() { return s_ready; }

void sideShowStatus(const char* line1, const char* line2) {
  s_banner_active = false;
  s_hud_active = false;
  drawTwoLines(line1, line2);
}

void sideShowRadarInfo() {
  if (!s_ready || s_banner_active) {
    return;
  }
  s_hud_active = true;
  s_last_cycle_ms = millis();
  paintRadarHud();
}

void sideShowThemeFlash() {
  if (!s_ready) {
    return;
  }

  s_g->fillScreen(colBg());
  drawCentered(ui::radar::themeCurrent().name, 52, colAccent(), 2);
  startBanner(config::kOledRangeFlashMs);
  flushSide();
}

void sideShowRangeFlash() {
  if (!s_ready) {
    return;
  }

  char label[16];
  const float ring3 = ui::radar::rangeCurrent().ring3_km;
  if (ui::radar::useMiles()) {
    const int mi = static_cast<int>(lroundf(ring3 / 1.609344f));
    snprintf(label, sizeof(label), "%d Miles", mi);
  } else {
    const int km = static_cast<int>(lroundf(ring3));
    snprintf(label, sizeof(label), "%d Km", km);
  }

  s_g->fillScreen(colBg());
  drawCentered(label, 52, colAccent(), 2);
  startBanner(config::kOledRangeFlashMs);
  flushSide();
}

void sideShowAircraftFlash(const char* callsign, const char* type) {
  if (!s_ready) {
    return;
  }

  char primary[12];
  primary[0] = '\0';
  if (callsign != nullptr && callsign[0] != '\0') {
    snprintf(primary, sizeof(primary), "%s", callsign);
  } else if (type != nullptr && type[0] != '\0') {
    snprintf(primary, sizeof(primary), "%s", type);
  } else {
    snprintf(primary, sizeof(primary), "ACFT");
  }

  s_g->fillScreen(colBg());
  const bool has_type =
      type != nullptr && type[0] != '\0' && strcmp(type, primary) != 0;
  if (has_type) {
    drawCentered(primary, 40, colAccent(), 2);
    drawCentered(type, 72, colType(), 1);
  } else {
    drawCentered(primary, 52, colAccent(), 2);
  }
  startBanner(config::kOledAircraftFlashMs);
  flushSide();
}

void sideAdvanceCard() {
  if (!s_ready || !s_hud_active) {
    return;
  }
  s_banner_active = false;
  const size_t n = services::adsb::aircraftCount();
  if (n == 0) {
    paintRadarHud();
    return;
  }
  s_card_index = (s_card_index + 1) % static_cast<int>(n);
  s_last_cycle_ms = millis();
  paintRadarHud();
}

void sidePoll() {
  if (!s_ready) {
    return;
  }

  if (s_banner_active) {
    if (static_cast<long>(millis() - s_banner_until_ms) >= 0) {
      s_banner_active = false;
      if (s_hud_active) {
        paintRadarHud();
      }
    }
    return;
  }

  if (!s_hud_active) {
    return;
  }

  const size_t n = services::adsb::aircraftCount();
  if (n <= 1) {
    return;
  }
  if (millis() - s_last_cycle_ms < config::kSideCycleMs) {
    return;
  }
  s_last_cycle_ms = millis();
  s_card_index = (s_card_index + 1) % static_cast<int>(n);
  paintRadarHud();
}
