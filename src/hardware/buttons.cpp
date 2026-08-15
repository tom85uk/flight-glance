#include "hardware/buttons.h"

#include <Arduino.h>

#include "config.h"

namespace {

struct Button {
  gpio_num_t pin;
  bool down;
  bool tap_pending;
  unsigned long down_ms;
};

Button s_theme{config::kThemeButtonPin, false, false, 0};
Button s_range{config::kRangeButtonPin, false, false, 0};
Button s_next{config::kNextButtonPin, false, false, 0};

bool pinDown(gpio_num_t pin) {
  return digitalRead(static_cast<uint8_t>(pin)) == LOW;
}

void pollButton(Button* b) {
  const bool down = pinDown(b->pin);
  const unsigned long now = millis();
  if (down && !b->down) {
    b->down = true;
    b->down_ms = now;
  } else if (!down && b->down) {
    b->down = false;
    if (now - b->down_ms >= config::kButtonDebounceMs) {
      b->tap_pending = true;
    }
  }
}

bool consume(Button* b) {
  if (!b->tap_pending) {
    return false;
  }
  b->tap_pending = false;
  return true;
}

}  // namespace

void buttonsInit() {
  if (!config::kButtonsEnabled) {
    return;
  }
  pinMode(static_cast<uint8_t>(config::kThemeButtonPin), INPUT_PULLUP);
  pinMode(static_cast<uint8_t>(config::kRangeButtonPin), INPUT_PULLUP);
  pinMode(static_cast<uint8_t>(config::kNextButtonPin), INPUT_PULLUP);
  Serial.printf("Buttons: theme=GPIO %d  range=GPIO %d  next=GPIO %d (to GND)\n",
                static_cast<int>(config::kThemeButtonPin),
                static_cast<int>(config::kRangeButtonPin),
                static_cast<int>(config::kNextButtonPin));
}

void buttonsPoll() {
  if (!config::kButtonsEnabled) {
    return;
  }
  pollButton(&s_theme);
  pollButton(&s_range);
  pollButton(&s_next);
}

bool themeButtonConsumeTap() { return consume(&s_theme); }
bool rangeButtonConsumeTap() { return consume(&s_range); }
bool nextButtonConsumeTap() { return consume(&s_next); }
