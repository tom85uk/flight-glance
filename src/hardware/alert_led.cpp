#include "hardware/alert_led.h"

#include <Arduino.h>

#include "config.h"

namespace {

uint8_t s_flashes_left = 0;
bool s_pulse_on = false;
unsigned long s_phase_until_ms = 0;
bool s_touch_held = false;

void setLed(bool on) {
  if (!config::kAlertLedEnabled) {
    return;
  }
  digitalWrite(static_cast<uint8_t>(config::kAlertLedPin), on ? HIGH : LOW);
}

void applyLed() {
  if (s_touch_held || (s_flashes_left > 0 && s_pulse_on)) {
    setLed(true);
  } else {
    setLed(false);
  }
}

void startFlashSequence(uint8_t count) {
  if (count == 0) {
    return;
  }
  // Prefer a fresh multi-flash over cutting a single short blip short.
  if (s_flashes_left > 0 && count <= s_flashes_left) {
    return;
  }
  s_flashes_left = count;
  s_pulse_on = true;
  s_phase_until_ms = millis() + config::kAlertLedFlashMs;
  applyLed();
}

}  // namespace

void alertLedInit() {
  if (!config::kAlertLedEnabled) {
    return;
  }
  pinMode(static_cast<uint8_t>(config::kAlertLedPin), OUTPUT);
  s_flashes_left = 0;
  s_pulse_on = false;
  s_phase_until_ms = 0;
  s_touch_held = false;
  setLed(false);
}

void alertLedPoll() {
  if (s_flashes_left == 0) {
    return;
  }
  if (static_cast<long>(millis() - s_phase_until_ms) < 0) {
    return;
  }

  if (s_pulse_on) {
    // End of ON pulse → gap (or finish after last pulse).
    s_pulse_on = false;
    --s_flashes_left;
    if (s_flashes_left == 0) {
      applyLed();
      return;
    }
    s_phase_until_ms = millis() + config::kAlertLedFlashGapMs;
    applyLed();
    return;
  }

  // End of gap → next ON pulse.
  s_pulse_on = true;
  s_phase_until_ms = millis() + config::kAlertLedFlashMs;
  applyLed();
}

void alertLedFlashOnce() { startFlashSequence(1); }

void alertLedFlashTwice() { startFlashSequence(2); }

void alertLedSetTouchHeld(bool held) {
  s_touch_held = held;
  applyLed();
}
