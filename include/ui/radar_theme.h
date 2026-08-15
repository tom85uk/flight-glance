#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::radar {

constexpr int kSize = 240;
constexpr int kCenterX = kSize / 2;
constexpr int kCenterY = kSize / 2;

/** Outermost grid ring (inside edge labels). */
constexpr int kGridOuterRadius = 107;

/** N: offset from top edge (top_center, negative = up). */
constexpr int kCardinalNorthOffsetY = -1;
/** S: offset from bottom edge (bottom_center, positive = down). */
constexpr int kCardinalSouthOffsetY = 3;

/** Gap between scale label right edge and outer ring on the east spoke (px). */
constexpr int kScaleGapFromOuterRing = 6;

/** Target cap height (px) for N/S/E/W. */
constexpr int kCardinalLabelHeightPx = 14;
/** Scale label is this many px shorter than cardinals. */
constexpr int kScaleBelowCardinalPx = 3;

constexpr int kRingCount = 4;

/** Shared grid stroke: drawWideLine half-width (~2 px total); rings use the same px count. */
constexpr float kGridStrokeHalfWidth = 1.0f;

constexpr int kCenterDotRadius = 2;

/** Track vector: ground distance covered in this many seconds at current gs. */
constexpr float kAircraftTrackHorizonSec = 60.0f;
/** Minimum visible vector when gs > 0 (px). */
constexpr int kAircraftSpeedLineMinPx = 2;
/** Track line length uses this outer_km, not the active range preset. */
constexpr float kAircraftTrackRefOuterKm = 13.3f;
/** Shorter than full 60 s horizon at ref scale; ×1.5 length boost applied. */
constexpr float kAircraftTrackLengthScale = 1.5f / 5.0f;
/** drawWideLine half-width for speed vectors (~2 px total). */
constexpr float kAircraftTrackLineHalfWidth = 1.0f;
/** Speed vector starts this far from icon center toward heading. */
constexpr int kAircraftVectorStartPx = 8;

constexpr float kRunwayLineWidthPx = 2.0f;
constexpr float kRunwayLineHalfWidth = kRunwayLineWidthPx * 0.5f;
constexpr int kRunwayLabelHeightPx = kCardinalLabelHeightPx;
constexpr int kRunwayLabelGapPx = 3;
/** Gap from plane icon edge to tag block (px). */
constexpr int kAircraftLabelGapPx = 2;
/** Keep symbol centroid inside outer ring by at least this inset (px). */
constexpr int kAircraftInsideRingInsetPx = 10;

/** Rotating sweep line (0° = north, clockwise). */
constexpr float kSweepLineHalfWidth = 0.6f;
constexpr int kSweepTrailCount = 1;

/** Beyond-ring traffic: bearing cues on screen rim (correct direction, fixed radius). */
constexpr int kBeyondRingDotRadiusPx = 4;
constexpr int kBeyondRingScreenMarginPx = 2;
/** Target cap height (px) for aircraft tags (bold, slightly above scale label). */
constexpr int kAircraftTagLabelHeightPx = 13;

struct ThemeRgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct ThemePalette {
  const char* name;
  ThemeRgb bg;
  ThemeRgb grid;
  ThemeRgb aircraft;
  ThemeRgb track;
  ThemeRgb tag_type;
  ThemeRgb tag_alt;
  ThemeRgb runway;
  ThemeRgb runway_label;
  ThemeRgb label;
  ThemeRgb sweep;
};

/** Load saved theme (default Amber). Call once at boot. */
void themeInit();
/** Cycle theme and save to flash. */
void themeNext();
/** Step theme backward (e.g. undo a press when a long-hold starts). */
void themePrev();
uint8_t themeIndex();
size_t themeCount();
const char* themeName(uint8_t index);
const ThemePalette& themeCurrent();
/** Portal theme index as decimal string. */
void saveThemeFromPortal(const char* value);

extern uint16_t kColorBackground;
extern uint16_t kColorGrid;
extern uint16_t kColorLabel;
extern uint16_t kColorCenter;
extern uint16_t kColorAircraft;
extern uint16_t kColorTrackVector;
extern uint16_t kColorTagType;
extern uint16_t kColorTagAltitude;
extern uint16_t kColorRunway;
extern uint16_t kColorRunwayLabel;
extern uint16_t kColorSweep;

}  // namespace ui::radar
