#include "ui/radar_display.h"

#include <Arduino.h>
#include <lgfx/v1/lgfx_fonts.hpp>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "ui/plane_icons.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/runway_overlay.h"

#include <Preferences.h>

namespace lgfx_fonts = lgfx::v1::fonts;

namespace ui {
namespace radar {

uint16_t kColorBackground = 0x0000;
uint16_t kColorGrid = 0x0320;
uint16_t kColorLabel = 0xFFFF;
uint16_t kColorCenter = 0xFFFF;
uint16_t kColorAircraft = 0x001F;
uint16_t kColorTrackVector = 0xFFFF;
uint16_t kColorTagType = 0x5DFF;
uint16_t kColorTagAltitude = 0xFFE0;
uint16_t kColorRunway = 0x4D5F;
uint16_t kColorRunwayLabel = 0x7DFF;
uint16_t kColorSweep = 0xFFE0;

}  // namespace radar

namespace {

bool s_label_metrics_ready = false;
bool s_cardinal_use_vlw = false;
bool s_scale_use_vlw = false;
float s_cardinal_vlw_size = 0.56f;
float s_scale_vlw_size = 0.50f;
const lgfx::GFXfont* s_cardinal_gfx = &lgfx_fonts::FreeSansBold12pt7b;
const lgfx::GFXfont* s_scale_gfx = &lgfx_fonts::FreeSansBold9pt7b;

int s_scale_label_max_w = 0;
int s_scale_label_h = 0;

lgfx::LovyanGFX* s_draw = &tft;
LGFX_Sprite s_grid(&tft);
LGFX_Sprite s_plane_icon(&tft);
bool s_grid_ready = false;
bool s_grid_valid = false;
bool s_plane_icon_ready = false;

struct DirtyRect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
};

/** Icon or rim-dot areas from the last aircraft paint. */
constexpr size_t kMaxDirtyRects = services::adsb::kMaxAircraft;
DirtyRect s_prev_dirty[kMaxDirtyRects];
size_t s_prev_dirty_count = 0;

/** Sweep line from the previous frame (restored from the frozen grid). */
float s_sweep_head_deg = 0.0f;
float s_prev_sweep_head_deg = 0.0f;
bool s_prev_sweep_valid = false;
unsigned long s_last_sweep_frame_ms = 0;

/** Sweep runs from loop() so it uses the same SPI context as the first paint. */
volatile bool s_sweep_enabled = false;
bool s_sweep_pref_enabled = true;
bool s_sweep_prefs_loaded = false;

constexpr char kSweepPrefsNamespace[] = "planeradar";
constexpr char kSweepPrefsKey[] = "sweepOn";

/** Scratch for blitting grid patches back onto the panel. Keep small so Wi‑Fi keeps heap. */
constexpr int kBlitTileW = 32;
constexpr int kBlitTileH = 32;
uint16_t s_blit_tile[kBlitTileW * kBlitTileH];

class DrawScope {
 public:
  explicit DrawScope(lgfx::LovyanGFX& gfx) : prev_(s_draw) { s_draw = &gfx; }
  ~DrawScope() { s_draw = prev_; }

 private:
  lgfx::LovyanGFX* prev_;
};

int absDiff(int a, int b) { return std::abs(a - b); }

int measureGfxHeight(const lgfx::GFXfont& font) {
  tft.setFont(&font);
  tft.setTextSize(1);
  return tft.fontHeight();
}

int measureVlwHeight(float size) {
  tft.setTextSize(size);
  return tft.fontHeight();
}

float findVlwSizeForHeight(int target_px) {
  float lo = 0.25f;
  float hi = 1.2f;
  for (int i = 0; i < 16; ++i) {
    const float mid = (lo + hi) * 0.5f;
    if (measureVlwHeight(mid) < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void applyScaleStyle();

const lgfx::GFXfont* pickGfxFontClosest(
    int target_px, const lgfx::GFXfont* const* candidates, size_t count) {
  const lgfx::GFXfont* best = candidates[0];
  int best_diff = absDiff(measureGfxHeight(*best), target_px);

  for (size_t i = 1; i < count; ++i) {
    const int diff = absDiff(measureGfxHeight(*candidates[i]), target_px);
    if (diff < best_diff) {
      best_diff = diff;
      best = candidates[i];
    }
  }
  return best;
}

void initLabelMetrics() {
  if (s_label_metrics_ready) {
    return;
  }

  const int cardinal_target = radar::kCardinalLabelHeightPx;

  if (displayFontIsSmooth()) {
    s_cardinal_use_vlw = true;
    s_cardinal_vlw_size = findVlwSizeForHeight(cardinal_target);
    const int cardinal_h = measureVlwHeight(s_cardinal_vlw_size);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    s_scale_use_vlw = true;
    s_scale_vlw_size = findVlwSizeForHeight(scale_target);
  } else {
    const lgfx::GFXfont* cardinal_candidates[] = {&lgfx_fonts::FreeSansBold12pt7b,
                                                  &lgfx_fonts::FreeSansBold9pt7b};
    s_cardinal_gfx =
        pickGfxFontClosest(cardinal_target, cardinal_candidates, 2);
    s_cardinal_use_vlw = false;

    const int cardinal_h = measureGfxHeight(*s_cardinal_gfx);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    const lgfx::GFXfont* scale_candidates[] = {&lgfx_fonts::FreeSansBold9pt7b,
                                               &lgfx_fonts::FreeSansBold12pt7b};
    s_scale_gfx = pickGfxFontClosest(scale_target, scale_candidates, 2);
    s_scale_use_vlw = false;
  }

  applyScaleStyle();
  s_scale_label_h = tft.fontHeight();
  s_scale_label_max_w = 0;
  char label[12];
  for (size_t i = 0; i < radar::kRangePresetCount; ++i) {
    for (bool miles : {false, true}) {
      radar::formatRing3Label(label, sizeof(label), radar::kRangePresets[i].ring3_km,
                              miles);
      const int w = tft.textWidth(label);
      if (w > s_scale_label_max_w) {
        s_scale_label_max_w = w;
      }
    }
  }

  s_label_metrics_ready = true;
}

/** GC9A01 on this board needs R/B swapped for correct amber/red output. */
uint16_t panelColor(uint8_t r, uint8_t g, uint8_t b) {
  if (config::kDisplayRgbOrder) {
    return tft.color565(b, g, r);
  }
  return tft.color565(r, g, b);
}

void initPalette() {
  const radar::ThemePalette& theme = radar::themeCurrent();
  radar::kColorBackground =
      panelColor(theme.bg.r, theme.bg.g, theme.bg.b);
  radar::kColorGrid = panelColor(theme.grid.r, theme.grid.g, theme.grid.b);
  radar::kColorLabel =
      panelColor(theme.label.r, theme.label.g, theme.label.b);
  radar::kColorCenter =
      panelColor(theme.aircraft.r, theme.aircraft.g, theme.aircraft.b);
  radar::kColorAircraft =
      panelColor(theme.aircraft.r, theme.aircraft.g, theme.aircraft.b);
  radar::kColorTrackVector =
      panelColor(theme.track.r, theme.track.g, theme.track.b);
  radar::kColorTagType =
      panelColor(theme.tag_type.r, theme.tag_type.g, theme.tag_type.b);
  radar::kColorTagAltitude =
      panelColor(theme.tag_alt.r, theme.tag_alt.g, theme.tag_alt.b);
  radar::kColorRunway =
      panelColor(theme.runway.r, theme.runway.g, theme.runway.b);
  radar::kColorRunwayLabel = panelColor(theme.runway_label.r, theme.runway_label.g,
                                        theme.runway_label.b);
  radar::kColorSweep =
      panelColor(theme.sweep.r, theme.sweep.g, theme.sweep.b);

  // Icon bitmap is rebuilt with the active theme color each palette init.
  if (s_plane_icon_ready) {
    s_plane_icon.deleteSprite();
    s_plane_icon_ready = false;
  }
}

constexpr float kKmPerDeg = 111.0f;

void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km) {
  *dx_km =
      static_cast<float>(lon - services::location::lon()) * kKmPerDeg;
  *dy_km =
      static_cast<float>(lat - services::location::lat()) * kKmPerDeg;
  *dist_km = sqrtf((*dx_km) * (*dx_km) + (*dy_km) * (*dy_km));
}

float innerRingMaxKm() {
  const float outer_km = radar::rangeCurrent().outer_km;
  return outer_km * (static_cast<float>(radar::kGridOuterRadius -
                                       radar::kAircraftInsideRingInsetPx) /
                     static_cast<float>(radar::kGridOuterRadius));
}

/** Flat lat/lon as x/y: 1° ≈ 111 km, north = screen up. */
void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const float outer_km = radar::rangeCurrent().outer_km;
  const float px_per_km = static_cast<float>(radar::kGridOuterRadius) / outer_km;

  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);

  *out_x = radar::kCenterX + static_cast<int>(lroundf(dx_km * px_per_km));
  *out_y = radar::kCenterY - static_cast<int>(lroundf(dy_km * px_per_km));
}

bool isInsideOuterRingKm(float dist_km) { return dist_km <= innerRingMaxKm(); }

int distSqFromCenter(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy;
}

bool isInsideOuterRing(int x, int y) {
  const int max_r = radar::kGridOuterRadius - radar::kAircraftInsideRingInsetPx;
  return distSqFromCenter(x, y) <= max_r * max_r;
}

/** Rim dot from true bearing; always on screen edge (even if target is 50+ km away). */
bool beyondRingEdgeDotFromLatLon(float lat, float lon, int* out_x, int* out_y) {
  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);
  if (dist_km < 0.01f) {
    return false;
  }
  if (isInsideOuterRingKm(dist_km)) {
    return false;
  }

  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int rim_r = radar::kCenterX - radar::kBeyondRingScreenMarginPx;
  const float angle_rad = atan2f(dx_km, dy_km);

  *out_x = cx + static_cast<int>(lroundf(sinf(angle_rad) * rim_r));
  *out_y = cy - static_cast<int>(lroundf(cosf(angle_rad) * rim_r));
  return true;
}

void drawBeyondRingDot(int x, int y) {
  if (s_draw->getColorDepth() > 8) {
    s_draw->fillSmoothCircle(x, y, radar::kBeyondRingDotRadiusPx,
                             radar::kColorAircraft);
  } else {
    s_draw->fillCircle(x, y, radar::kBeyondRingDotRadiusPx, radar::kColorAircraft);
  }
}

bool ensurePlaneIcon() {
  if (s_plane_icon_ready) {
    return true;
  }
  s_plane_icon.setColorDepth(16);
  if (!s_plane_icon.createSprite(PLANE_ICON_SIZE, PLANE_ICON_SIZE)) {
    Serial.println("radar: plane icon sprite alloc failed");
    return false;
  }
  s_plane_icon.setSwapBytes(true);
  s_plane_icon.pushImage(0, 0, PLANE_ICON_SIZE, PLANE_ICON_SIZE, planeUp,
                         PLANE_ICON_TRANSPARENT);
  s_plane_icon.setSwapBytes(false);

  // Recolor mask pixels to panel-correct amber.
  for (int y = 0; y < PLANE_ICON_SIZE; ++y) {
    for (int x = 0; x < PLANE_ICON_SIZE; ++x) {
      if (s_plane_icon.readPixel(x, y) != PLANE_ICON_TRANSPARENT) {
        s_plane_icon.drawPixel(x, y, radar::kColorAircraft);
      }
    }
  }

  s_plane_icon_ready = true;
  return true;
}

/** Mini-radar plane silhouette, rotated to heading (degrees, 0 = north). */
void drawAircraftIcon(int cx, int cy, float heading_deg) {
  float heading = fmodf(heading_deg, 360.0f);
  if (heading < 0.0f) {
    heading += 360.0f;
  }

  if (ensurePlaneIcon()) {
    s_plane_icon.pushRotateZoom(s_draw, cx, cy, heading, 1.0f, 1.0f,
                                PLANE_ICON_TRANSPARENT);
    return;
  }

  s_draw->fillCircle(cx, cy, 3, radar::kColorAircraft);
}

bool clampDirtyRect(DirtyRect* r) {
  if (r->w <= 0 || r->h <= 0) {
    return false;
  }
  int x = r->x;
  int y = r->y;
  int w = r->w;
  int h = r->h;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > radar::kSize) {
    w = radar::kSize - x;
  }
  if (y + h > radar::kSize) {
    h = radar::kSize - y;
  }
  if (w <= 0 || h <= 0) {
    return false;
  }
  r->x = static_cast<int16_t>(x);
  r->y = static_cast<int16_t>(y);
  r->w = static_cast<int16_t>(w);
  r->h = static_cast<int16_t>(h);
  return true;
}

void addDirtyRect(DirtyRect* rects, size_t* count, size_t max_count, int x,
                  int y, int w, int h) {
  if (*count >= max_count) {
    return;
  }
  DirtyRect r;
  r.x = static_cast<int16_t>(x);
  r.y = static_cast<int16_t>(y);
  r.w = static_cast<int16_t>(w);
  r.h = static_cast<int16_t>(h);
  if (!clampDirtyRect(&r)) {
    return;
  }
  rects[*count] = r;
  ++(*count);
}

struct AircraftDrawItem {
  size_t index = 0;
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

struct BeyondDotDrawItem {
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

void sortDrawItemsFarFirst(AircraftDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const AircraftDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void sortBeyondDotsFarFirst(BeyondDotDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const BeyondDotDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void drawAircraft(DirtyRect* dirty, size_t* dirty_count, size_t dirty_max) {
  initLabelMetrics();
  if (dirty_count != nullptr) {
    *dirty_count = 0;
  }

  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();

  AircraftDrawItem items[services::adsb::kMaxAircraft];
  BeyondDotDrawItem dots[services::adsb::kMaxAircraft];
  size_t draw_count = 0;
  size_t dot_count = 0;

  for (size_t i = 0; i < n; ++i) {
    float dx_km = 0.0f;
    float dy_km = 0.0f;
    float dist_km = 0.0f;
    offsetKmFromCenter(planes[i].lat, planes[i].lon, &dx_km, &dy_km, &dist_km);

    if (isInsideOuterRingKm(dist_km)) {
      int x = 0;
      int y = 0;
      latLonToScreen(planes[i].lat, planes[i].lon, &x, &y);
      items[draw_count].index = i;
      items[draw_count].x = x;
      items[draw_count].y = y;
      items[draw_count].dist_sq = distSqFromCenter(x, y);
      ++draw_count;
      continue;
    }

    int dot_x = 0;
    int dot_y = 0;
    if (!beyondRingEdgeDotFromLatLon(planes[i].lat, planes[i].lon, &dot_x,
                                     &dot_y)) {
      continue;
    }
    dots[dot_count].x = dot_x;
    dots[dot_count].y = dot_y;
    dots[dot_count].dist_sq = distSqFromCenter(dot_x, dot_y);
    ++dot_count;
  }

  sortBeyondDotsFarFirst(dots, dot_count);
  for (size_t d = 0; d < dot_count; ++d) {
    const int x = dots[d].x;
    const int y = dots[d].y;
    drawBeyondRingDot(x, y);
    if (dirty != nullptr && dirty_count != nullptr) {
      const int pad = radar::kBeyondRingDotRadiusPx + 2;
      addDirtyRect(dirty, dirty_count, dirty_max, x - pad, y - pad, pad * 2 + 1,
                   pad * 2 + 1);
    }
  }

  sortDrawItemsFarFirst(items, draw_count);
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    const int x = items[d].x;
    const int y = items[d].y;
    drawAircraftIcon(x, y, planes[i].nose_deg);
    if (dirty != nullptr && dirty_count != nullptr) {
      constexpr int kIconHalf = 12;
      addDirtyRect(dirty, dirty_count, dirty_max, x - kIconHalf, y - kIconHalf,
                   kIconHalf * 2 + 1, kIconHalf * 2 + 1);
    }
  }
}

void applyCardinalStyle() {
  if (s_cardinal_use_vlw && s_draw->getColorDepth() > 8) {
    displayFontSetSmoothSize(*s_draw, s_cardinal_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_cardinal_gfx);
  }
}

void applyScaleStyle() {
  if (s_scale_use_vlw && s_draw->getColorDepth() > 8) {
    displayFontSetSmoothSize(*s_draw, s_scale_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_scale_gfx);
  }
}

void drawCardinalLabel(const char* text, int x, int y, textdatum_t datum) {
  applyCardinalStyle();
  s_draw->setTextDatum(datum);
  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawScaleLabelWithBackground(const char* text, int x, int y) {
  applyScaleStyle();
  s_draw->setTextDatum(textdatum_t::middle_right);

  const int tw = s_draw->textWidth(text);
  const int th = s_draw->fontHeight();
  constexpr int kPadX = 3;
  constexpr int kPadY = 2;

  const int left = x - tw - kPadX;
  const int top = y - th / 2 - kPadY;

  s_draw->fillRect(left, top, tw + kPadX * 2, th + kPadY * 2,
                   radar::kColorBackground);
  s_draw->setTextColor(radar::kColorGrid, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawGridRing(int cx, int cy, int r, uint16_t color) {
  if (r <= 0) {
    return;
  }
  const int thickness =
      std::max(1, static_cast<int>(radar::kGridStrokeHalfWidth * 2.0f));
  for (int i = 0; i < thickness && r - i > 0; ++i) {
    s_draw->drawCircle(cx, cy, r - i, color);
  }
}

void drawRings(int cx, int cy, int outer_radius, int ring_count) {
  if (ring_count <= 0) {
    return;
  }
  for (int i = 1; i <= ring_count; ++i) {
    const int r = (outer_radius * i) / ring_count;
    drawGridRing(cx, cy, r, radar::kColorGrid);
  }
}

void drawCrosshairs(int cx, int cy, int radius, uint16_t color) {
  s_draw->drawWideLine(cx, cy - radius, cx, cy + radius,
                       radar::kGridStrokeHalfWidth, color);
  s_draw->drawWideLine(cx - radius, cy, cx + radius, cy,
                       radar::kGridStrokeHalfWidth, color);
}

void drawSpokes(int cx, int cy, int radius, int spoke_count, uint16_t color) {
  if (spoke_count <= 0 || radius <= 0) {
    return;
  }
  for (int i = 0; i < spoke_count; ++i) {
    const float deg = (360.0f * static_cast<float>(i)) /
                      static_cast<float>(spoke_count);
    const float rad = deg * 0.01745329252f;
    const int x2 = cx + static_cast<int>(lroundf(radius * sinf(rad)));
    const int y2 = cy - static_cast<int>(lroundf(radius * cosf(rad)));
    s_draw->drawWideLine(cx, cy, x2, y2, radar::kGridStrokeHalfWidth, color);
  }
}

void drawReticleTicks(int cx, int cy, int radius, uint16_t color) {
  constexpr int kTick = 10;
  const int inner = radius - kTick;
  if (inner <= 0) {
    return;
  }
  s_draw->drawWideLine(cx, cy - radius, cx, cy - inner,
                       radar::kGridStrokeHalfWidth, color);
  s_draw->drawWideLine(cx, cy + inner, cx, cy + radius,
                       radar::kGridStrokeHalfWidth, color);
  s_draw->drawWideLine(cx - radius, cy, cx - inner, cy,
                       radar::kGridStrokeHalfWidth, color);
  s_draw->drawWideLine(cx + inner, cy, cx + radius, cy,
                       radar::kGridStrokeHalfWidth, color);
}

void drawGridStyle(int cx, int cy, int grid_r) {
  switch (radar::gridStyleIndex()) {
    case 1:  // Rings
      drawRings(cx, cy, grid_r, radar::kRingCount);
      break;
    case 2:  // Cross
      drawGridRing(cx, cy, grid_r, radar::kColorGrid);
      drawCrosshairs(cx, cy, grid_r, radar::kColorGrid);
      break;
    case 3:  // Spokes
      drawRings(cx, cy, grid_r, 3);
      drawSpokes(cx, cy, grid_r, 8, radar::kColorGrid);
      break;
    case 4:  // Reticle
      drawRings(cx, cy, grid_r, 2);
      drawReticleTicks(cx, cy, grid_r, radar::kColorGrid);
      drawCrosshairs(cx, cy, grid_r / 3, radar::kColorGrid);
      break;
    case 5:  // Minimal
      drawGridRing(cx, cy, grid_r, radar::kColorGrid);
      break;
    case 0:  // Classic
    default:
      drawRings(cx, cy, grid_r, radar::kRingCount);
      drawCrosshairs(cx, cy, grid_r, radar::kColorGrid);
      break;
  }
}

void drawCenterDot(int cx, int cy) {
  // 8-bit sprites have no alpha; fillSmoothCircle jumps to a null vtable.
  if (s_draw->getColorDepth() > 8) {
    s_draw->fillSmoothCircle(cx, cy, radar::kCenterDotRadius, radar::kColorCenter);
  } else {
    s_draw->fillCircle(cx, cy, radar::kCenterDotRadius, radar::kColorCenter);
  }
}

void drawCardinalLabels() {
  if (!radar::showCardinals()) {
    return;
  }
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int edge = radar::kSize - 1;

  drawCardinalLabel("N", cx, radar::kCardinalNorthOffsetY, textdatum_t::top_center);
  drawCardinalLabel("S", cx, edge + radar::kCardinalSouthOffsetY,
                    textdatum_t::bottom_center);
  drawCardinalLabel("W", 0, cy, textdatum_t::middle_left);
  drawCardinalLabel("E", edge, cy, textdatum_t::middle_right);
}

int scaleLabelAnchorX(int cx, int outer_radius) {
  return cx + outer_radius - radar::kScaleGapFromOuterRing;
}

void drawScaleLabel(int cx, int cy, int outer_radius) {
  char scale_label[12];
  radar::formatCurrentRing3Label(scale_label, sizeof(scale_label));
  drawScaleLabelWithBackground(scale_label,
                               scaleLabelAnchorX(cx, outer_radius), cy);
}

template <typename Gfx>
void drawStaticGrid(Gfx& gfx) {
  initLabelMetrics();
  const DrawScope scope(gfx);
  if (gfx.getColorDepth() > 8) {
    displayFontEnsureLoaded(gfx);
  }
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int grid_r = radar::kGridOuterRadius;

  gfx.fillScreen(radar::kColorBackground);
  drawGridStyle(cx, cy, grid_r);
  initPalette();
  runway::drawLargeAirportRunways(gfx);
  drawCenterDot(cx, cy);
  drawCardinalLabels();
  gfx.setTextDatum(textdatum_t::top_left);
}

bool tryCreateGrid(uint8_t depth) {
  s_grid.deleteSprite();
  s_grid.setPsram(false);
  s_grid.setColorDepth(depth);
  if (!s_grid.createSprite(radar::kSize, radar::kSize)) {
    return false;
  }
  if (depth == 8) {
    s_grid.createPalette();
  }
  return true;
}

bool ensureGridSprite() {
  if (s_grid_ready) {
    return true;
  }
  Serial.printf("radar: heap before grid %u  max %u\n",
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()));
  // 16-bit is 115 KB and steals the DMA heap Wi‑Fi already claimed.
  if (tryCreateGrid(8)) {
    s_grid_ready = true;
    Serial.printf("radar: 8-bit grid cache, heap %u\n",
                  static_cast<unsigned>(ESP.getFreeHeap()));
    return true;
  }
  Serial.println("radar: grid sprite alloc failed");
  return false;
}

void rebuildGridCache() {
  // 16-bit 240×240 needs 115 KB contiguous; this chip only has ~110 KB after
  // Wi‑Fi. 8-bit sprites crash in LovyanGFX alpha (fillSmoothCircle).
  s_grid_valid = false;
}

float wrapDeg(float deg) {
  while (deg < 0.0f) {
    deg += 360.0f;
  }
  while (deg >= 360.0f) {
    deg -= 360.0f;
  }
  return deg;
}

float sweepStepDeg() {
  return 360.0f / static_cast<float>(config::kRadarSweepPeriodMs) *
         static_cast<float>(config::kRadarSweepFrameMs);
}

void sweepTip(float deg, int* tip_x, int* tip_y) {
  constexpr float kDegToRad = 0.01745329252f;
  const float d = wrapDeg(deg);
  const float rad = d * kDegToRad;
  const float r = static_cast<float>(radar::kGridOuterRadius);
  *tip_x = radar::kCenterX + static_cast<int>(lroundf(sinf(rad) * r));
  *tip_y = radar::kCenterY - static_cast<int>(lroundf(cosf(rad) * r));
}

void blitGridRectToPanel(int x, int y, int w, int h) {
  for (int ty = 0; ty < h; ty += kBlitTileH) {
    const int th = std::min(kBlitTileH, h - ty);
    for (int tx = 0; tx < w; tx += kBlitTileW) {
      const int tw = std::min(kBlitTileW, w - tx);
      s_grid.readRect(x + tx, y + ty, tw, th, s_blit_tile);
      tft.pushImage(x + tx, y + ty, tw, th, s_blit_tile);
    }
  }
}

void restorePreviousAircraftAreas() {
  if (!s_grid_valid) {
    return;
  }
  for (size_t i = 0; i < s_prev_dirty_count; ++i) {
    const DirtyRect& r = s_prev_dirty[i];
    blitGridRectToPanel(r.x, r.y, r.w, r.h);
  }
}

void restoreSweepCorridor(float deg) {
  if (!s_grid_valid) {
    return;
  }
  int tip_x = 0;
  int tip_y = 0;
  sweepTip(deg, &tip_x, &tip_y);

  // A few segment boxes — far fewer SPI transactions than 7×7 tiles.
  constexpr int kPad = 3;
  constexpr int kSegs = 4;
  for (int i = 0; i < kSegs; ++i) {
    const float t0 = static_cast<float>(i) / static_cast<float>(kSegs);
    const float t1 = static_cast<float>(i + 1) / static_cast<float>(kSegs);
    const int x0 = radar::kCenterX +
                   static_cast<int>(lroundf((tip_x - radar::kCenterX) * t0));
    const int y0 = radar::kCenterY +
                   static_cast<int>(lroundf((tip_y - radar::kCenterY) * t0));
    const int x1 = radar::kCenterX +
                   static_cast<int>(lroundf((tip_x - radar::kCenterX) * t1));
    const int y1 = radar::kCenterY +
                   static_cast<int>(lroundf((tip_y - radar::kCenterY) * t1));
    DirtyRect r;
    r.x = static_cast<int16_t>(std::min(x0, x1) - kPad);
    r.y = static_cast<int16_t>(std::min(y0, y1) - kPad);
    r.w = static_cast<int16_t>(std::abs(x1 - x0) + kPad * 2 + 1);
    r.h = static_cast<int16_t>(std::abs(y1 - y0) + kPad * 2 + 1);
    if (clampDirtyRect(&r)) {
      blitGridRectToPanel(r.x, r.y, r.w, r.h);
    }
  }
}

void restorePreviousSweepArea() {
  if (!s_prev_sweep_valid) {
    return;
  }
  const float step = sweepStepDeg();
  for (int i = 0; i < radar::kSweepTrailCount; ++i) {
    restoreSweepCorridor(s_prev_sweep_head_deg - step * static_cast<float>(i));
  }
}

uint16_t sweepTrailColor(int trail_index) {
  const radar::ThemeRgb& sweep = radar::themeCurrent().sweep;
  const float t =
      1.0f - (static_cast<float>(trail_index) /
              static_cast<float>(radar::kSweepTrailCount));
  const uint8_t r = static_cast<uint8_t>(sweep.r * t);
  const uint8_t g = static_cast<uint8_t>(sweep.g * t * 0.85f);
  const uint8_t b = static_cast<uint8_t>(sweep.b * t * 0.5f);
  return panelColor(r, g, b);
}

void drawSweepOnPanel() {
  const float head = s_sweep_head_deg;
  const float step = sweepStepDeg();

  for (int i = radar::kSweepTrailCount - 1; i >= 0; --i) {
    const float deg = head - step * static_cast<float>(i);
    int tip_x = 0;
    int tip_y = 0;
    sweepTip(deg, &tip_x, &tip_y);
    const uint16_t color =
        (i == 0) ? radar::kColorSweep : sweepTrailColor(i);
    const float half = (i == 0) ? radar::kSweepLineHalfWidth
                                : radar::kSweepLineHalfWidth * 0.75f;
    tft.drawWideLine(radar::kCenterX, radar::kCenterY, tip_x, tip_y, half,
                     color);
  }

  tft.fillSmoothCircle(radar::kCenterX, radar::kCenterY, radar::kCenterDotRadius,
                       radar::kColorCenter);

  s_prev_sweep_head_deg = head;
  s_prev_sweep_valid = true;
}

void paintAircraftOnPanel() {
  const DrawScope scope(tft);
  displayFontEnsureLoaded(tft);
  services::adsb::lockAircraft();
  drawAircraft(s_prev_dirty, &s_prev_dirty_count, kMaxDirtyRects);
  services::adsb::unlockAircraft();
  tft.setTextDatum(textdatum_t::top_left);
}

void paintDynamicOverlays() {
  if (s_sweep_pref_enabled) {
    drawSweepOnPanel();
  } else {
    s_prev_sweep_valid = false;
  }
  paintAircraftOnPanel();
}

void paintFull() {
  rebuildGridCache();
  s_prev_dirty_count = 0;
  s_prev_sweep_valid = false;

  if (s_grid_valid) {
    s_grid.pushSprite(0, 0);
  } else {
    const DrawScope scope(tft);
    drawStaticGrid(tft);
  }
  paintDynamicOverlays();
}

void paintAircraftRefresh() {
  if (!s_grid_valid) {
    paintFull();
    return;
  }
  restorePreviousSweepArea();
  restorePreviousAircraftAreas();
  paintDynamicOverlays();
}

void restrokeStaticGrid() {
  const DrawScope scope(tft);
  displayFontEnsureLoaded(tft);
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int grid_r = radar::kGridOuterRadius;
  drawGridStyle(cx, cy, grid_r);
  runway::drawLargeAirportRunways(tft);
  drawCenterDot(cx, cy);
  drawCardinalLabels();
  tft.setTextDatum(textdatum_t::top_left);
}

void eraseSweepLine(float deg) {
  int tip_x = 0;
  int tip_y = 0;
  sweepTip(deg, &tip_x, &tip_y);
  tft.drawWideLine(radar::kCenterX, radar::kCenterY, tip_x, tip_y,
                   radar::kSweepLineHalfWidth + 1.0f, radar::kColorBackground);
}

void tickSweep() {
  if (!s_sweep_pref_enabled) {
    return;
  }
  const unsigned long now = millis();
  if (s_last_sweep_frame_ms != 0 &&
      now - s_last_sweep_frame_ms < config::kRadarSweepFrameMs) {
    return;
  }
  s_last_sweep_frame_ms = now;

  if (s_grid_valid) {
    restorePreviousSweepArea();
  } else if (s_prev_sweep_valid) {
    eraseSweepLine(s_prev_sweep_head_deg);
    restrokeStaticGrid();
  }
  s_sweep_head_deg = wrapDeg(s_sweep_head_deg + sweepStepDeg());
  drawSweepOnPanel();
  paintAircraftOnPanel();
}

void clearSweepFromPanel() {
  if (s_grid_valid) {
    restorePreviousSweepArea();
  } else if (s_prev_sweep_valid) {
    eraseSweepLine(s_prev_sweep_head_deg);
    restrokeStaticGrid();
  }
  s_prev_sweep_valid = false;
  if (s_prev_dirty_count > 0) {
    const DrawScope scope(tft);
    displayFontEnsureLoaded(tft);
    services::adsb::lockAircraft();
    drawAircraft(nullptr, nullptr, 0);
    services::adsb::unlockAircraft();
    tft.setTextDatum(textdatum_t::top_left);
  }
}

void persistSweepPref(bool enabled) {
  Preferences prefs;
  if (!prefs.begin(kSweepPrefsNamespace, false)) {
    return;
  }
  prefs.putBool(kSweepPrefsKey, enabled);
  prefs.end();
}

void applySweepRuntimeFlag() {
  s_sweep_enabled = s_sweep_pref_enabled;
}

}  // namespace

void radarSpritesInit() {}

void radarDisplayDraw() {
  initPalette();
  initLabelMetrics();
  paintFull();
  applySweepRuntimeFlag();
}

void radarDisplayRefreshAircraft() {
  initPalette();
  paintAircraftRefresh();
}

void radarDisplayTick() {
  if (!s_sweep_enabled) {
    return;
  }
  tickSweep();
}

void radarDisplaySweepInit() {
  if (s_sweep_prefs_loaded) {
    return;
  }
  Preferences prefs;
  if (prefs.begin(kSweepPrefsNamespace, true)) {
    s_sweep_pref_enabled = prefs.getBool(kSweepPrefsKey, true);
    prefs.end();
  }
  s_sweep_prefs_loaded = true;
}

void radarDisplaySetSweepEnabled(bool enabled) {
  s_sweep_pref_enabled = enabled;
  s_sweep_prefs_loaded = true;
  persistSweepPref(enabled);
  if (!enabled && s_sweep_enabled) {
    clearSweepFromPanel();
  }
  applySweepRuntimeFlag();
  Serial.printf("Radar sweep: %s\n", enabled ? "on" : "off");
}

bool radarDisplaySweepEnabled() { return s_sweep_pref_enabled; }

}  // namespace ui
