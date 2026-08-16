#include "services/wifi_setup.h"

#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>

#include <cmath>
#include <cstdio>

#include <Preferences.h>
#include <esp_system.h>
#include <esp_wifi.h>

#ifdef WM_MDNS
#include <ESPmDNS.h>
#endif

#include "config.h"
#include "services/radar_location.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/portal_style.h"
#include "ui/status_screens.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

portMUX_TYPE s_boot_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_boot_tap_pending = false;
volatile bool s_boot_long_range_pending = false;
volatile bool s_boot_is_down = false;
volatile unsigned long s_boot_down_ms = 0;
bool s_long_press_handled = false;
bool s_range_long_press_handled = false;
bool s_boot_interrupt_attached = false;

void IRAM_ATTR onBootButtonIsr() {
  const bool down = digitalRead(config::kBootPin) == LOW;
  const unsigned long now = millis();
  portENTER_CRITICAL_ISR(&s_boot_mux);
  if (down) {
    s_boot_is_down = true;
    s_boot_down_ms = now;
  } else if (s_boot_is_down) {
    const unsigned long held = now - s_boot_down_ms;
    // Short tap only — long hold is handled in poll (range / Wi‑Fi reset).
    if (held >= config::kBootTapMinMs && held < config::kRangeLongHoldMs) {
      s_boot_tap_pending = true;
    }
    s_boot_is_down = false;
  }
  portEXIT_CRITICAL_ISR(&s_boot_mux);
}

void initBootButton() {
  pinMode(config::kBootPin, INPUT_PULLUP);
  if (s_boot_interrupt_attached) {
    return;
  }
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kBootPin)),
                  onBootButtonIsr, CHANGE);
  s_boot_interrupt_attached = true;
}

portMUX_TYPE s_touch_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_touch_tap_pending = false;
volatile bool s_touch_long_range_pending = false;
volatile bool s_touch_theme_undo_pending = false;
volatile bool s_touch_is_down = false;
volatile unsigned long s_touch_down_ms = 0;
volatile unsigned long s_touch_last_short_ms = 0;
/** True after a press-triggered theme latch this contact (may need undo on long-hold). */
volatile bool s_touch_theme_latched_this_press = false;
bool s_touch_interrupt_attached = false;
bool s_touch_long_press_handled = false;

void IRAM_ATTR onTouchButtonIsr() {
  const int level = digitalRead(config::kTouchPin);
  const bool down =
      config::kTouchActiveHigh ? (level == HIGH) : (level == LOW);
  const unsigned long now = millis();
  portENTER_CRITICAL_ISR(&s_touch_mux);
  if (down) {
    if (!s_touch_is_down) {
      s_touch_is_down = true;
      s_touch_down_ms = now;
      s_touch_theme_latched_this_press = false;
      // Fire theme on press (not release) so it feels instant.
      if (now - s_touch_last_short_ms >= config::kTouchTapDebounceMs) {
        s_touch_tap_pending = true;
        s_touch_last_short_ms = now;
        s_touch_theme_latched_this_press = true;
      }
    }
  } else {
    s_touch_is_down = false;
    s_touch_theme_latched_this_press = false;
  }
  portEXIT_CRITICAL_ISR(&s_touch_mux);
}

void initTouchButton() {
  if (!config::kTouchEnabled) {
    return;
  }
  // Active-high modules: pulldown so idle is LOW. Active-low: pullup.
  pinMode(config::kTouchPin,
          config::kTouchActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  if (s_touch_interrupt_attached) {
    return;
  }
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kTouchPin)),
                  onTouchButtonIsr, CHANGE);
  s_touch_interrupt_attached = true;
}

namespace {

/** Separate from planeradar prefs (rangeInit) to avoid NVS handle conflicts. */
constexpr char kWifiPrefsNamespace[] = "wifi";
constexpr char kPrefsForcePortalKey[] = "portal";

bool s_force_config_portal = false;
bool s_settings_changed = false;
WiFiManager s_wm;
bool s_wm_configured = false;

void ensureWifiManager();
void startLanWebPortal();
void stopLanWebPortal();
bool wifiLinkUp();

constexpr int kCoordParamLen = 20;
char s_lat_attrs[64] = " type=\"number\" step=\"0.000001\"";
char s_lon_attrs[64] = " type=\"number\" step=\"0.000001\"";

char s_home_note_html[220];
WiFiManagerParameter s_param_home_note(s_home_note_html);

char s_use_home_attrs[40] = "type=\"checkbox\"";
WiFiManagerParameter s_param_use_home("use_home", "Use home", "T", 2,
                                     s_use_home_attrs, WFM_LABEL_AFTER);

WiFiManagerParameter s_param_lat("radar_lat", "Latitude", "0", kCoordParamLen,
                                s_lat_attrs);
WiFiManagerParameter s_param_lon("radar_lon", "Longitude", "0", kCoordParamLen,
                                s_lon_attrs);

char s_range_html[520];
WiFiManagerParameter s_param_range_html(s_range_html);

char s_theme_html[1800];
WiFiManagerParameter s_param_theme_html(s_theme_html);

char s_grid_html[420];
WiFiManagerParameter s_param_grid_html(s_grid_html);

char s_sweep_attrs[40] = "type=\"checkbox\"";
WiFiManagerParameter s_param_sweep("show_sweep", "Show radar sweep", "T", 2,
                                  s_sweep_attrs, WFM_LABEL_AFTER);

char s_miles_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_miles("use_miles", "Display distances in miles", "T", 2,
                                   s_miles_checkbox_attrs, WFM_LABEL_AFTER);

char s_runways_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_runways("show_runways", "Show airport runways", "T", 2,
                                     s_runways_checkbox_attrs, WFM_LABEL_AFTER);

char s_cardinals_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_cardinals("show_cardinals", "Show N E S W", "T", 2,
                                       s_cardinals_checkbox_attrs, WFM_LABEL_AFTER);

bool nearHome(double lat, double lon) {
  return fabs(lat - services::location::homeLat()) < 1e-4 &&
         fabs(lon - services::location::homeLon()) < 1e-4;
}

char s_portal_head[12288];
uint8_t s_portal_head_theme = 255;

void rebuildPortalHead() {
  portalBuildHeadHtml(s_portal_head, sizeof(s_portal_head),
                      ui::radar::themeCurrent());
  s_portal_head_theme = ui::radar::themeIndex();
}

void maybeRebuildPortalHead() {
  if (s_portal_head_theme != ui::radar::themeIndex()) {
    rebuildPortalHead();
  }
}

void refreshPortalParamDefaults() {
  const bool use_home = nearHome(services::location::lat(),
                                 services::location::lon());

  snprintf(s_home_note_html, sizeof(s_home_note_html),
           "<div class=\"hud-home\"><b>HOME</b> // %.5f, %.5f</div>",
           services::location::homeLat(), services::location::homeLon());

  snprintf(s_use_home_attrs, sizeof(s_use_home_attrs), "type=\"checkbox\"%s",
           use_home ? " checked" : "");
  s_param_use_home.setValue("T", 2);

  snprintf(s_lat_attrs, sizeof(s_lat_attrs),
           " type=\"number\" step=\"0.000001\"%s",
           use_home ? " disabled" : "");
  snprintf(s_lon_attrs, sizeof(s_lon_attrs),
           " type=\"number\" step=\"0.000001\"%s",
           use_home ? " disabled" : "");

  char lat_buf[kCoordParamLen + 1];
  char lon_buf[kCoordParamLen + 1];
  snprintf(lat_buf, sizeof(lat_buf), "%.6f", services::location::lat());
  snprintf(lon_buf, sizeof(lon_buf), "%.6f", services::location::lon());
  s_param_lat.setValue(lat_buf, kCoordParamLen);
  s_param_lon.setValue(lon_buf, kCoordParamLen);

  const uint8_t range_idx = ui::radar::rangeIndex();
  char opt0[12];
  char opt1[12];
  char opt2[12];
  char opt3[12];
  ui::radar::formatRing3Label(opt0, sizeof(opt0),
                              ui::radar::kRangePresets[0].outer_km,
                              ui::radar::useMiles());
  ui::radar::formatRing3Label(opt1, sizeof(opt1),
                              ui::radar::kRangePresets[1].outer_km,
                              ui::radar::useMiles());
  ui::radar::formatRing3Label(opt2, sizeof(opt2),
                              ui::radar::kRangePresets[2].outer_km,
                              ui::radar::useMiles());
  ui::radar::formatRing3Label(opt3, sizeof(opt3),
                              ui::radar::kRangePresets[3].outer_km,
                              ui::radar::useMiles());
  snprintf(
      s_range_html, sizeof(s_range_html),
      "<br/><label for='radar_range'>Radar radius</label><br/>"
      "<select id='radar_range' name='radar_range'>"
      "<option value='0'%s>%s</option>"
      "<option value='1'%s>%s</option>"
      "<option value='2'%s>%s</option>"
      "<option value='3'%s>%s</option>"
      "</select><br/>",
      range_idx == 0 ? " selected" : "", opt0, range_idx == 1 ? " selected" : "",
      opt1, range_idx == 2 ? " selected" : "", opt2,
      range_idx == 3 ? " selected" : "", opt3);

  {
    const uint8_t theme_idx = ui::radar::themeIndex();
    const size_t n = ui::radar::themeCount();
    size_t used = static_cast<size_t>(snprintf(
        s_theme_html, sizeof(s_theme_html),
        "<br/><label for='radar_theme'>Colour theme</label><br/>"
        "<select id='radar_theme' name='radar_theme'>"));
    for (size_t i = 0; i < n && used + 160 < sizeof(s_theme_html); ++i) {
      const ui::radar::ThemePalette& pal =
          ui::radar::themeAt(static_cast<uint8_t>(i));
      used += static_cast<size_t>(snprintf(
          s_theme_html + used, sizeof(s_theme_html) - used,
          "<option value='%u'%s data-bg='%u,%u,%u' data-ac='%u,%u,%u' "
          "data-sw='%u,%u,%u' data-gr='%u,%u,%u'>%s</option>",
          static_cast<unsigned>(i), (i == theme_idx) ? " selected" : "",
          pal.bg.r, pal.bg.g, pal.bg.b, pal.aircraft.r, pal.aircraft.g,
          pal.aircraft.b, pal.sweep.r, pal.sweep.g, pal.sweep.b, pal.grid.r,
          pal.grid.g, pal.grid.b, pal.name));
    }
    snprintf(s_theme_html + used, sizeof(s_theme_html) - used, "</select><br/>");
  }

  {
    const uint8_t grid_idx = ui::radar::gridStyleIndex();
    const size_t n = ui::radar::gridStyleCount();
    size_t used = static_cast<size_t>(snprintf(
        s_grid_html, sizeof(s_grid_html),
        "<br/><label for='radar_grid'>Grid style</label><br/>"
        "<select id='radar_grid' name='radar_grid'>"));
    for (size_t i = 0; i < n && used + 48 < sizeof(s_grid_html); ++i) {
      used += static_cast<size_t>(snprintf(
          s_grid_html + used, sizeof(s_grid_html) - used,
          "<option value='%u'%s>%s</option>", static_cast<unsigned>(i),
          (i == grid_idx) ? " selected" : "",
          ui::radar::gridStyleName(static_cast<uint8_t>(i))));
    }
    snprintf(s_grid_html + used, sizeof(s_grid_html) - used, "</select><br/>");
  }

  snprintf(s_sweep_attrs, sizeof(s_sweep_attrs), "type=\"checkbox\"%s",
           ui::radarDisplaySweepEnabled() ? " checked" : "");
  s_param_sweep.setValue("T", 2);

  snprintf(s_miles_checkbox_attrs, sizeof(s_miles_checkbox_attrs), "type=\"checkbox\"%s",
           ui::radar::useMiles() ? " checked" : "");
  s_param_miles.setValue("T", 2);
  snprintf(s_runways_checkbox_attrs, sizeof(s_runways_checkbox_attrs),
           "type=\"checkbox\"%s", ui::radar::showRunways() ? " checked" : "");
  s_param_runways.setValue("T", 2);
  snprintf(s_cardinals_checkbox_attrs, sizeof(s_cardinals_checkbox_attrs),
           "type=\"checkbox\"%s", ui::radar::showCardinals() ? " checked" : "");
  s_param_cardinals.setValue("T", 2);
  rebuildPortalHead();
}

bool portalArgChecked(const char* id) {
  return s_wm.server && s_wm.server->hasArg(id);
}

void onPortalParamsSaved() {
  if (portalArgChecked("use_home")) {
    services::location::restoreHome();
  } else {
    const char* lat = s_wm.server && s_wm.server->hasArg("radar_lat")
                          ? s_wm.server->arg("radar_lat").c_str()
                          : s_param_lat.getValue();
    const char* lon = s_wm.server && s_wm.server->hasArg("radar_lon")
                          ? s_wm.server->arg("radar_lon").c_str()
                          : s_param_lon.getValue();
    if (!services::location::saveFromStrings(lat, lon)) {
      Serial.println("Invalid lat/lon in portal — keeping previous location");
    }
  }

  if (s_wm.server && s_wm.server->hasArg("radar_range")) {
    ui::radar::saveRangeIndexFromPortal(
        s_wm.server->arg("radar_range").c_str());
  }
  if (s_wm.server && s_wm.server->hasArg("radar_theme")) {
    ui::radar::saveThemeFromPortal(s_wm.server->arg("radar_theme").c_str());
  }
  if (s_wm.server && s_wm.server->hasArg("radar_grid")) {
    ui::radar::saveGridStyleFromPortal(s_wm.server->arg("radar_grid").c_str());
  }

  ui::radarDisplaySetSweepEnabled(portalArgChecked("show_sweep"));
  ui::radar::saveMilesFromPortal(portalArgChecked("use_miles") ? "T" : "");
  ui::radar::saveRunwaysFromPortal(portalArgChecked("show_runways") ? "T" : "");
  ui::radar::saveCardinalsFromPortal(portalArgChecked("show_cardinals") ? "T" : "");
  s_settings_changed = true;
  refreshPortalParamDefaults();
}

void attachPortalParams(WiFiManager& wm) {
  refreshPortalParamDefaults();
  wm.addParameter(&s_param_home_note);
  wm.addParameter(&s_param_use_home);
  wm.addParameter(&s_param_lat);
  wm.addParameter(&s_param_lon);
  wm.addParameter(&s_param_range_html);
  wm.addParameter(&s_param_theme_html);
  wm.addParameter(&s_param_grid_html);
  wm.addParameter(&s_param_sweep);
  wm.addParameter(&s_param_miles);
  wm.addParameter(&s_param_runways);
  wm.addParameter(&s_param_cardinals);
  wm.setSaveParamsCallback(onPortalParamsSaved);
}

void markForceConfigPortal() {
  s_force_config_portal = true;
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, false)) {
    return;
  }
  prefs.putBool(kPrefsForcePortalKey, true);
  prefs.end();
}

bool consumeForceConfigPortal() {
  if (s_force_config_portal) {
    s_force_config_portal = false;
    Preferences prefs;
    if (prefs.begin(kWifiPrefsNamespace, false)) {
      prefs.remove(kPrefsForcePortalKey);
      prefs.end();
    }
    return true;
  }

  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    return false;
  }
  const bool pending = prefs.getBool(kPrefsForcePortalKey, false);
  prefs.end();
  if (!pending) {
    return false;
  }

  if (prefs.begin(kWifiPrefsNamespace, false)) {
    prefs.remove(kPrefsForcePortalKey);
    prefs.end();
  }
  return true;
}

bool storedWifiCredentials() {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_STA);
    delay(50);
  }

  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
    return false;
  }
  return conf.sta.ssid[0] != '\0';
}

void eraseWifiCredentials() {
  stopLanWebPortal();
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_OFF);
  delay(100);

  ensureWifiManager();
  WiFi.persistent(true);
  s_wm.resetSettings();
  s_wm.erase();
  WiFi.disconnect(true, true);
  WiFi.persistent(false);

  WiFi.mode(WIFI_OFF);
  delay(100);
}

void resetWifiCredentials() {
  markForceConfigPortal();
  eraseWifiCredentials();
  services::location::clear();
  ui::radar::unitsReset();
  Serial.println("WiFi credentials, location, and units cleared");
}

void onConfigPortalApStarted(WiFiManager*) {
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  statusScreenPortal();
#ifdef WM_MDNS
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("Setup portal: http://%s.local (or http://%s)\n",
                  config::kPortalHostname, config::kPortalIp);
  } else {
    Serial.printf("Setup portal: http://%s (mDNS unavailable)\n", config::kPortalIp);
  }
#else
  Serial.printf("Setup portal: http://%s\n", config::kPortalIp);
#endif
}

bool wifiLinkUp() {
  return WiFi.status() == WL_CONNECTED &&
         WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

constexpr char kPortalConfigureRadarHtml[] =
    "<form action='/param' method='get'>"
    "<button type='submit'>Configure Radar</button>"
    "</form><br/>\n";

void ensureWifiManager() {
  if (s_wm_configured) {
    return;
  }
  s_wm.setTitle("Flight Radar");
  s_wm.setDarkMode(true);
  s_wm.setCustomMenuHTML(kPortalConfigureRadarHtml);
  // Move params off the WiFi page (sets _paramsInWifi=false), then restore our
  // menu so "Configure Radar" stays instead of WiFiManager's "Setup" button.
  s_wm.setParamsPage(true);
  std::vector<const char*> menu = {"wifi", "custom", "info", "exit"};
  s_wm.setMenu(menu);
  s_wm.setShowInfoUpdate(false);
  s_wm.setConfigPortalTimeout(config::kWifiPortalTimeoutSec);
  s_wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                           IPAddress(255, 255, 255, 0));
  s_wm.setHostname(config::kPortalHostname);
  s_wm.setAPCallback(onConfigPortalApStarted);
  attachPortalParams(s_wm);
  s_wm.setCustomHeadElement(s_portal_head);
  s_wm_configured = true;
}

void startLanWebPortal() {
  if (!wifiLinkUp() || s_wm.getWebPortalActive() ||
      s_wm.getConfigPortalActive()) {
    return;
  }
  refreshPortalParamDefaults();
  WiFi.mode(WIFI_STA);
  s_wm.setConfigPortalBlocking(false);
#ifdef WM_MDNS
  MDNS.end();
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
  }
#endif
  s_wm.startWebPortal();
  Serial.printf("LAN config: http://%s.local or http://%s\n",
                config::kPortalHostname, WiFi.localIP().toString().c_str());
}

void stopLanWebPortal() {
  if (!s_wm.getWebPortalActive()) {
    return;
  }
  s_wm.stopWebPortal();
#ifdef WM_MDNS
  MDNS.end();
#endif
}

void prepareSta() {
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setAutoReconnect(true);
}

void startStaConnect(const String& ssid, const String& pass) {
  prepareSta();
  if (ssid.length() > 0) {
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    WiFi.begin();
  }
}

bool waitForLinkQuiet(unsigned long attempt_ms) {
  const unsigned long deadline = millis() + attempt_ms;
  while (millis() < deadline) {
    if (wifiLinkUp()) {
      return true;
    }
    delay(50);
  }
  return wifiLinkUp();
}

bool waitForLinkWithUi(const char* ssid_for_ui, unsigned long attempt_ms) {
  const unsigned long deadline = millis() + attempt_ms;
  while (millis() < deadline) {
    if (wifiLinkUp()) {
      return true;
    }
    bootButtonPollLongPress();
    statusScreenConnectingTick();
    delay(config::kWifiConnectingFrameMs);
  }
  return wifiLinkUp();
}

bool tryConnectWithUi(const String& ssid, const String& pass, bool show_ui) {
  if (wifiLinkUp()) {
    return true;
  }

  const char* ui_ssid = ssid.length() > 0 ? ssid.c_str() : "network";
  if (show_ui) {
    statusScreenConnectingBegin(ui_ssid);
  }

  for (uint8_t attempt = 1; attempt <= config::kWifiConnectAttempts; ++attempt) {
    if (attempt > 1) {
      Serial.printf("WiFi connect retry %u/%u\n", attempt,
                    config::kWifiConnectAttempts);
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(400);
    }

    startStaConnect(ssid, pass);

    const bool up = show_ui
                        ? waitForLinkWithUi(ui_ssid, config::kWifiConnectAttemptMs)
                        : waitForLinkQuiet(config::kWifiConnectAttemptMs);
    if (up) {
      return true;
    }
  }

  return false;
}

bool connectHardcodedNetwork(bool show_ui) {
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
  Serial.printf("Connecting with secrets.h SSID: %s\n", WIFI_SSID);
  return tryConnectWithUi(String(WIFI_SSID), String(WIFI_PASSWORD), show_ui);
#else
  (void)show_ui;
  return false;
#endif
}

bool connectSavedNetwork(bool show_ui) {
  if (!storedWifiCredentials()) {
    return false;
  }

  ensureWifiManager();
  const String ssid = s_wm.getWiFiSSID();
  if (ssid.length() == 0) {
    return false;
  }
  const String pass = s_wm.getWiFiPass();
  return tryConnectWithUi(ssid, pass, show_ui);
}

bool openConfigPortal() {
  stopLanWebPortal();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
  statusScreenPortal();
  s_wm.setConfigPortalBlocking(false);
  s_wm.startConfigPortal(config::kPortalApName);
  while (s_wm.getConfigPortalActive()) {
    bootButtonPollLongPress();
    touchButtonPollLongPress();
    if (s_wm.process()) {
      return true;
    }
    delay(10);
  }
  return wifiLinkUp();
}

}  // namespace

bool wifiShowsSetupScreenOnBoot() {
  if (s_force_config_portal) {
    return true;
  }
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    return false;
  }
  const bool pending = prefs.getBool(kPrefsForcePortalKey, false);
  prefs.end();
  return pending;
}

bool wifiBootButtonPressed() {
  return digitalRead(config::kBootPin) == LOW;
}

void bootButtonInit() { initBootButton(); }

void touchButtonInit() { initTouchButton(); }

bool bootButtonConsumeTap() {
  portENTER_CRITICAL(&s_boot_mux);
  const bool tap = s_boot_tap_pending;
  if (tap) {
    s_boot_tap_pending = false;
  }
  portEXIT_CRITICAL(&s_boot_mux);
  return tap;
}

bool touchButtonConsumeTap() {
  portENTER_CRITICAL(&s_touch_mux);
  const bool tap = s_touch_tap_pending;
  if (tap) {
    s_touch_tap_pending = false;
  }
  portEXIT_CRITICAL(&s_touch_mux);
  return tap;
}

bool touchButtonIsDown() {
  if (!config::kTouchEnabled) {
    return false;
  }
  const int level = digitalRead(config::kTouchPin);
  return config::kTouchActiveHigh ? (level == HIGH) : (level == LOW);
}

bool bootButtonConsumeLongRange() {
  portENTER_CRITICAL(&s_boot_mux);
  const bool tap = s_boot_long_range_pending;
  if (tap) {
    s_boot_long_range_pending = false;
  }
  portEXIT_CRITICAL(&s_boot_mux);
  return tap;
}

bool touchButtonConsumeLongRange() {
  portENTER_CRITICAL(&s_touch_mux);
  const bool tap = s_touch_long_range_pending;
  if (tap) {
    s_touch_long_range_pending = false;
  }
  portEXIT_CRITICAL(&s_touch_mux);
  return tap;
}

bool touchButtonConsumeThemeUndo() {
  portENTER_CRITICAL(&s_touch_mux);
  const bool undo = s_touch_theme_undo_pending;
  if (undo) {
    s_touch_theme_undo_pending = false;
  }
  portEXIT_CRITICAL(&s_touch_mux);
  return undo;
}

void bootButtonPollLongPress() {
  if (wifiBootButtonPressed()) {
    portENTER_CRITICAL(&s_boot_mux);
    if (!s_boot_is_down) {
      s_boot_is_down = true;
      s_boot_down_ms = millis();
    }
    const unsigned long down_ms = s_boot_down_ms;
    portEXIT_CRITICAL(&s_boot_mux);

    const unsigned long held = millis() - down_ms;
    if (!s_range_long_press_handled && held >= config::kRangeLongHoldMs) {
      s_range_long_press_handled = true;
      portENTER_CRITICAL(&s_boot_mux);
      s_boot_long_range_pending = true;
      portEXIT_CRITICAL(&s_boot_mux);
      Serial.println("BOOT long-hold — cycle range");
    }
    if (!s_long_press_handled && held >= config::kWifiResetHoldMs) {
      s_long_press_handled = true;
      Serial.println("BOOT held — resetting WiFi");
      wifiResetCredentialsAndReboot();
    }
  } else {
    portENTER_CRITICAL(&s_boot_mux);
    s_boot_is_down = false;
    portEXIT_CRITICAL(&s_boot_mux);
    s_long_press_handled = false;
    s_range_long_press_handled = false;
  }
}

void touchButtonPollLongPress() {
  if (!config::kTouchEnabled) {
    return;
  }
  if (!touchButtonIsDown()) {
    s_touch_long_press_handled = false;
    return;
  }

  portENTER_CRITICAL(&s_touch_mux);
  if (s_touch_down_ms == 0) {
    s_touch_down_ms = millis();
  }
  const unsigned long held_from = s_touch_down_ms;
  portEXIT_CRITICAL(&s_touch_mux);

  if (!s_touch_long_press_handled &&
      millis() - held_from >= config::kRangeLongHoldMs) {
    s_touch_long_press_handled = true;
    portENTER_CRITICAL(&s_touch_mux);
    // Cancel unconsumed short tap, or undo theme if it already applied.
    if (s_touch_tap_pending) {
      s_touch_tap_pending = false;
      s_touch_theme_latched_this_press = false;
    } else if (s_touch_theme_latched_this_press) {
      s_touch_theme_undo_pending = true;
      s_touch_theme_latched_this_press = false;
    }
    s_touch_long_range_pending = true;
    portEXIT_CRITICAL(&s_touch_mux);
    Serial.println("Touch long-hold — cycle range");
  }
}

void wifiResetCredentialsAndReboot() {
  resetWifiCredentials();
  statusScreenWifiReset();
  delay(800);
  esp_restart();
}

bool wifiReconnect() {
  initBootButton();
  Serial.println("WiFi reconnecting...");
  if (connectHardcodedNetwork(true)) {
    return true;
  }
  return connectSavedNetwork(true);
}

bool s_ntp_started = false;

void wifiEnsureNtp() {
  if (s_ntp_started || !wifiLinkUp()) {
    return;
  }
  // UK civil time (GMT/BST). NTP is UDP and non-blocking after this call.
  configTzTime("GMT0BST,M3.5.0/1,M10.5.0/2", "pool.ntp.org",
               "time.cloudflare.com");
  s_ntp_started = true;
  Serial.println("NTP: Europe/London");
}

void wifiLoop() {
  ensureWifiManager();
  if (wifiLinkUp()) {
    wifiEnsureNtp();
    if (!s_wm.getWebPortalActive() && !s_wm.getConfigPortalActive()) {
      startLanWebPortal();
    }
    if (s_wm.getWebPortalActive() || s_wm.getConfigPortalActive()) {
      maybeRebuildPortalHead();
      bootButtonPollLongPress();
      touchButtonPollLongPress();
      s_wm.process();
    }
  } else {
    stopLanWebPortal();
  }
}

bool wifiConsumeSettingsChanged() {
  if (!s_settings_changed) {
    return false;
  }
  s_settings_changed = false;
  return true;
}

bool wifiConnectEarly() {
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
  if (wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    return true;
  }

  Serial.printf("WiFi early connect (before displays): %s\n", WIFI_SSID);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  if (!waitForLinkQuiet(12000)) {
    Serial.printf("WiFi early connect timeout, status=%d\n",
                  static_cast<int>(WiFi.status()));
    return false;
  }

  WiFi.setAutoReconnect(true);
  Serial.printf("WiFi up: %s  IP %s\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str());
  return true;
#else
  return false;
#endif
}

bool wifiSetupConnect() {
  initBootButton();

  if (wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  const bool force_portal = consumeForceConfigPortal();
  WiFi.setAutoReconnect(false);

  if (force_portal) {
    eraseWifiCredentials();
    WiFi.mode(WIFI_OFF);
    delay(100);
    ensureWifiManager();
    Serial.println("Opening WiFi setup portal (after reset)");
    if (openConfigPortal() && wifiLinkUp()) {
      WiFi.setAutoReconnect(true);
      Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str());
      return true;
    }
    Serial.println("WiFi connection failed");
    statusScreenConnectFailed();
    return false;
  }

  Serial.println("Connecting to WiFi...");

  if (connectHardcodedNetwork(false)) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

#if defined(WIFI_SSID)
  Serial.println("secrets.h WiFi failed — not opening setup portal");
  statusScreenConnectFailed();
  return false;
#else
  ensureWifiManager();

  if (storedWifiCredentials() && connectSavedNetwork(true)) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  if (storedWifiCredentials()) {
    Serial.println("Saved WiFi could not connect — opening setup portal");
  } else {
    Serial.println("No saved WiFi — opening setup portal");
  }

  if (openConfigPortal() && wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("WiFi connection failed");
  statusScreenConnectFailed();
  return false;
#endif
}
