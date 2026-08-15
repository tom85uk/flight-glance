#pragma once

#include <freertos/FreeRTOS.h>

#include "hardware/lgfx_config.hpp"

extern LGFX tft;
extern LGFX_Side sideTft;

void displayInit();

void displayLock();
void displayUnlock();
bool displayTryLock(TickType_t ticks);

class DisplayLock {
 public:
  DisplayLock();
  ~DisplayLock();
};
