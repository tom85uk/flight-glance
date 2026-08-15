#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Allocate the radar grid cache after Wi‑Fi is up. */
void radarSpritesInit();

/** Move aircraft without rewriting the radar background (dirty-rect update). */
void radarDisplayRefreshAircraft();

/** Advance/draw the rotating sweep when the radar view is active. */
void radarDisplayTick();

/** Load sweep preference from flash (call once at boot with rangeInit). */
void radarDisplaySweepInit();
void radarDisplaySetSweepEnabled(bool enabled);
bool radarDisplaySweepEnabled();

}  // namespace ui
