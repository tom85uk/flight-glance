/**
 * Flight Glance — mini-radar-c3 firmware on ESP32 DevKit,
 * with GC9A01 radar + ST7735 side card and secrets.h Wi‑Fi.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "config.h"
#include "hardware/alert_led.h"
#include "hardware/buttons.h"
#include "hardware/display.h"
#include "hardware/side_display.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/reverse_geocode.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/status_screens.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
TaskHandle_t g_adsb_task = nullptr;

char g_seen_hex[services::adsb::kMaxAircraft][7];
size_t g_seen_count = 0;
bool g_seen_ready = false;

void handleButtons();
void ensureAdsbTask();

/** Keep Wi‑Fi portal + buttons alive while ADS-B HTTP is in progress. */
void networkPollWhileBusy() {
  wifiLoop();
  handleButtons();
  alertLedPoll();
  sidePoll();
  if (g_radar_visible) {
    ui::radarDisplayTick();
  }
}

bool hexAlreadySeen(const char* hex) {
  if (hex == nullptr || hex[0] == '\0') {
    return true;
  }
  for (size_t i = 0; i < g_seen_count; ++i) {
    if (strcmp(g_seen_hex[i], hex) == 0) {
      return true;
    }
  }
  return false;
}

void rememberCurrentAircraft() {
  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();
  g_seen_count = 0;
  if (planes == nullptr) {
    return;
  }
  for (size_t i = 0; i < n && g_seen_count < services::adsb::kMaxAircraft; ++i) {
    if (planes[i].hex[0] == '\0') {
      continue;
    }
    strncpy(g_seen_hex[g_seen_count], planes[i].hex, sizeof(g_seen_hex[0]));
    g_seen_hex[g_seen_count][sizeof(g_seen_hex[0]) - 1] = '\0';
    ++g_seen_count;
  }
}

/** Flash twice if any aircraft ICAO hex is new since the last poll. */
void flashIfNewAircraftPickedUp() {
  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();
  if (!g_seen_ready) {
    rememberCurrentAircraft();
    g_seen_ready = true;
    return;
  }

  bool found_new = false;
  const char* new_cs = nullptr;
  const char* new_type = nullptr;
  if (planes != nullptr) {
    for (size_t i = 0; i < n; ++i) {
      if (!hexAlreadySeen(planes[i].hex)) {
        found_new = true;
        new_cs = planes[i].callsign;
        new_type = services::adsb::typeLabel(planes[i]);
        break;
      }
    }
  }

  rememberCurrentAircraft();
  if (found_new) {
    alertLedFlashTwice();
    sideShowAircraftFlash(new_cs, new_type);
  }
}

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  sideShowRadarInfo();
  g_radar_visible = true;
  delay(100);
  ensureAdsbTask();
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (%.1f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  sideShowRangeFlash();
  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void onThemeTap() {
  ui::radar::themeNext();
  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
    sideShowThemeFlash();
  }
}

void handleButtons() {
  buttonsPoll();
  bootButtonPollLongPress();
  touchButtonPollLongPress();
  alertLedSetTouchHeld(touchButtonIsDown());

  if (touchButtonConsumeThemeUndo()) {
    ui::radar::themePrev();
  }

  const bool touch_range = touchButtonConsumeLongRange();
  const bool boot_range = bootButtonConsumeLongRange();
  const bool panel_range = rangeButtonConsumeTap();
  if (boot_range || touch_range || panel_range) {
    if (touch_range) {
      alertLedFlashOnce();
    }
    onRangeTap();
  }

  const bool touch_theme = touchButtonConsumeTap();
  const bool boot_theme = bootButtonConsumeTap();
  const bool panel_theme = themeButtonConsumeTap();
  if (boot_theme || touch_theme || panel_theme) {
    if (touch_theme) {
      alertLedFlashOnce();
    }
    onThemeTap();
  }

  if (nextButtonConsumeTap()) {
    sideAdvanceCard();
  }
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    return;
  }
  if (services::geocode::placeName()[0] == '\0') {
    services::geocode::refreshPlaceName(services::location::lat(),
                                        services::location::lon());
  }
}

void adsbTaskFn(void* /*arg*/) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      fetchAndDrawAircraft();
    }
    vTaskDelay(pdMS_TO_TICKS(config::kAdsbFetchIntervalMs));
  }
}

void ensureAdsbTask() {
  if (g_adsb_task != nullptr) {
    return;
  }
  // Below the UI loop (core 1, pri 1) so JSON parse cannot stall the sweep.
  xTaskCreatePinnedToCore(adsbTaskFn, "adsbFetch", 8192, nullptr, 0, &g_adsb_task,
                          1);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Flight Glance");

  // Radio first — GPIO 25/26 become SPI afterwards, and the font/display
  // buffers eat the DMA heap Wi‑Fi needs.
  wifiConnectEarly();

  bootButtonInit();
  touchButtonInit();
  buttonsInit();
  alertLedInit();
  displayInit();
  ui::radar::themeInit();
  sideInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
    sideShowStatus("WiFi setup", "PlaneRadar-Setup");
  }
  services::location::init();
  ui::radar::rangeInit();
  ui::radarDisplaySweepInit();

#if defined(WIFI_SSID)
  sideShowStatus("Connecting...", WIFI_SSID);
#else
  sideShowStatus("Connecting...", "WiFi...");
#endif
  Serial.printf("heap after displays: %u  max %u  dma %u\n",
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)));
  if (wifiSetupConnect()) {
    alertLedFlashOnce();
    delay(200);
    showRadarIfConnected();
  } else {
    sideShowStatus("WiFi failed", "check secrets/portal");
  }
}

void loop() {
  if (g_radar_visible) {
    ui::radarDisplayTick();
  }
  handleButtons();
  wifiLoop();
  alertLedPoll();
  sidePoll();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
      sideShowStatus("WiFi lost", "reconnecting...");
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        alertLedFlashOnce();
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else {
      if (wifiConsumeSettingsChanged()) {
        g_seen_ready = false;
        showRadarIfConnected();
      }
      if (services::adsb::consumeUpdated()) {
        flashIfNewAircraftPickedUp();
        sideShowRadarInfo();
      }
    }
  }

  delay(1);
}
