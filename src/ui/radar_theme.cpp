#include "ui/radar_theme.h"

#include <Preferences.h>

#include <cstdlib>
#include <cstring>

#include <Arduino.h>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsThemeKey[] = "themeIdx";
constexpr uint8_t kDefaultThemeIndex = 0;  // Amber

// clang-format off
constexpr ThemePalette kThemes[] = {
    {
        "Amber",
        {10, 6, 0},      // bg
        {120, 78, 0},    // grid
        {206, 149, 0},   // aircraft
        {255, 190, 40},  // track
        {255, 200, 80},  // tag type
        {230, 180, 90},  // tag alt
        {160, 110, 20},  // runway
        {220, 170, 60},  // runway label
        {255, 220, 140}, // label
        {255, 210, 80},  // sweep
    },
    {
        "Green",
        {0, 8, 0},
        {0, 140, 40},
        {40, 220, 80},
        {120, 255, 140},
        {160, 255, 180},
        {100, 220, 120},
        {0, 120, 60},
        {140, 230, 160},
        {180, 255, 190},
        {80, 255, 120},
    },
    {
        "Cyan",
        {0, 8, 12},
        {0, 110, 140},
        {40, 200, 230},
        {100, 230, 255},
        {140, 240, 255},
        {80, 200, 220},
        {0, 100, 130},
        {120, 220, 240},
        {180, 240, 255},
        {60, 230, 255},
    },
    {
        "Red",
        {10, 0, 0},
        {140, 30, 20},
        {230, 50, 40},
        {255, 120, 90},
        {255, 160, 140},
        {230, 100, 90},
        {150, 40, 30},
        {240, 140, 120},
        {255, 200, 190},
        {255, 80, 60},
    },
    {
        "Ice",
        {4, 8, 16},
        {80, 120, 180},
        {180, 210, 255},
        {200, 230, 255},
        {220, 240, 255},
        {160, 200, 240},
        {60, 100, 160},
        {180, 210, 255},
        {230, 240, 255},
        {200, 230, 255},
    },
    {
        "Gold",
        {12, 8, 0},
        {150, 110, 20},
        {255, 200, 40},
        {255, 220, 100},
        {255, 230, 140},
        {230, 190, 80},
        {170, 120, 20},
        {255, 210, 90},
        {255, 235, 160},
        {255, 215, 60},
    },
    {
        "Magenta",
        {10, 0, 10},
        {150, 30, 120},
        {240, 60, 200},
        {255, 120, 230},
        {255, 160, 240},
        {220, 100, 200},
        {140, 30, 110},
        {250, 140, 220},
        {255, 200, 240},
        {255, 80, 210},
    },
    {
        "Blue",
        {0, 4, 14},
        {30, 80, 180},
        {60, 140, 255},
        {100, 180, 255},
        {140, 200, 255},
        {80, 150, 240},
        {20, 60, 150},
        {120, 180, 255},
        {180, 210, 255},
        {70, 160, 255},
    },
    {
        "Lime",
        {4, 10, 0},
        {80, 160, 20},
        {160, 255, 40},
        {200, 255, 100},
        {220, 255, 140},
        {160, 230, 60},
        {70, 140, 20},
        {190, 255, 100},
        {220, 255, 170},
        {180, 255, 50},
    },
    {
        "Mono",
        {6, 6, 6},
        {90, 90, 90},
        {220, 220, 220},
        {255, 255, 255},
        {240, 240, 240},
        {190, 190, 190},
        {110, 110, 110},
        {220, 220, 220},
        {245, 245, 245},
        {230, 230, 230},
    },
    {
        "Orange",
        {12, 4, 0},
        {160, 70, 10},
        {255, 120, 20},
        {255, 160, 60},
        {255, 180, 100},
        {240, 130, 50},
        {170, 70, 15},
        {255, 160, 80},
        {255, 200, 140},
        {255, 140, 30},
    },
    {
        "Teal",
        {0, 10, 10},
        {0, 120, 110},
        {20, 220, 200},
        {80, 255, 230},
        {120, 255, 240},
        {60, 200, 190},
        {0, 110, 100},
        {100, 230, 220},
        {170, 255, 245},
        {40, 240, 220},
    },
};
// clang-format on

constexpr size_t kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);

uint8_t s_theme_index = kDefaultThemeIndex;

void persistThemeIndex() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  prefs.putUChar(kPrefsThemeKey, s_theme_index);
  prefs.end();
}

}  // namespace

void themeInit() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    s_theme_index = kDefaultThemeIndex;
    return;
  }
  const uint8_t saved = prefs.getUChar(kPrefsThemeKey, kDefaultThemeIndex);
  prefs.end();
  s_theme_index = (saved < kThemeCount) ? saved : kDefaultThemeIndex;
}

void themeNext() {
  s_theme_index =
      static_cast<uint8_t>((s_theme_index + 1) % kThemeCount);
  persistThemeIndex();
  Serial.printf("Radar theme: %s\n", themeCurrent().name);
}

void themePrev() {
  s_theme_index = static_cast<uint8_t>(
      (s_theme_index + kThemeCount - 1) % kThemeCount);
  persistThemeIndex();
  Serial.printf("Radar theme: %s\n", themeCurrent().name);
}

uint8_t themeIndex() { return s_theme_index; }

size_t themeCount() { return kThemeCount; }

const char* themeName(uint8_t index) {
  if (index >= kThemeCount) {
    return kThemes[kDefaultThemeIndex].name;
  }
  return kThemes[index].name;
}

const ThemePalette& themeCurrent() { return kThemes[s_theme_index]; }

void saveThemeFromPortal(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return;
  }
  char* end = nullptr;
  const long idx = strtol(value, &end, 10);
  if (end == value || (end != nullptr && *end != '\0')) {
    return;
  }
  if (idx < 0 || idx >= static_cast<long>(kThemeCount)) {
    return;
  }
  s_theme_index = static_cast<uint8_t>(idx);
  persistThemeIndex();
  Serial.printf("Radar theme: %s\n", themeCurrent().name);
}

}  // namespace ui::radar
