#pragma once

#include <cstddef>

namespace services::adsb {

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  char hex[7];
  char callsign[9];
  char type[8];
  char desc[40];
  char alt[12];
  char reg[10];
  char squawk[6];
};

/** Long type name from the API (`desc`), else ICAO type code (`t`). */
inline const char* typeLabel(const Aircraft& ac) {
  return ac.desc[0] != '\0' ? ac.desc : ac.type;
}

constexpr size_t kMaxAircraft = 64;

size_t aircraftCount();
const Aircraft* aircraftList();

void lockAircraft();
void unlockAircraft();
/** True once after a successful fetch until consumed. */
bool consumeUpdated();

/** Hook invoked during long HTTP I/O. Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/** Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

}  // namespace services::adsb
