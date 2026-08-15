#include "hardware/display.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"
#include "hardware/display_font.h"

LGFX tft;
LGFX_Side sideTft;

namespace {

SemaphoreHandle_t s_spi_mu = nullptr;

void ensureSpiMutex() {
  if (s_spi_mu == nullptr) {
    s_spi_mu = xSemaphoreCreateMutex();
  }
}

}  // namespace

void displayLock() {
  ensureSpiMutex();
  xSemaphoreTake(s_spi_mu, portMAX_DELAY);
}

void displayUnlock() { xSemaphoreGive(s_spi_mu); }

bool displayTryLock(TickType_t ticks) {
  ensureSpiMutex();
  return xSemaphoreTake(s_spi_mu, ticks) == pdTRUE;
}

DisplayLock::DisplayLock() { displayLock(); }
DisplayLock::~DisplayLock() { displayUnlock(); }

void displayInit() {
  pinMode(static_cast<uint8_t>(config::kDisplayPinCs), OUTPUT);
  pinMode(static_cast<uint8_t>(config::kSidePinCs), OUTPUT);
  digitalWrite(static_cast<uint8_t>(config::kDisplayPinCs), HIGH);
  digitalWrite(static_cast<uint8_t>(config::kSidePinCs), HIGH);

  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);

  sideTft.init();
  sideTft.setRotation(config::kSideRotation);
  sideTft.setTextWrap(false);

  displayFontInit();
}
