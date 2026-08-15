#pragma once

/** GPIO pull-ups + debounce state. Call once in setup(). */
void buttonsInit();

/** Sample the panel buttons. Call from loop and while HTTP is busy. */
void buttonsPoll();

/** Latched short taps (true once). */
bool themeButtonConsumeTap();
bool rangeButtonConsumeTap();
bool nextButtonConsumeTap();
