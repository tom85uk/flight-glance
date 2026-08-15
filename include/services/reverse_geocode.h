#pragma once

namespace services::geocode {

/** Cached place name for the radar center, or "" if unknown yet. */
const char* placeName();

/**
 * Resolve lat/lon to a locality name (HTTPS). No-op if offline or unchanged.
 * Call after WiFi is up, and again if the radar center changes.
 */
bool refreshPlaceName(double lat, double lon);

/** Clear cached name (e.g. after location reset). */
void clear();

}  // namespace services::geocode
