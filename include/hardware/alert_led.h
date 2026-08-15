#pragma once

/** Init alert LED pin (off). */
void alertLedInit();

/** Call from loop — ends a flash without blocking. */
void alertLedPoll();

/** Start a single short flash. */
void alertLedFlashOnce();

/** Two short flashes (e.g. new aircraft pickup). */
void alertLedFlashTwice();

/** Keep LED lit while capacitive touch is held (restores off when released). */
void alertLedSetTouchHeld(bool held);
