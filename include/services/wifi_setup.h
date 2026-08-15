#pragma once

/** True when the next boot should show the setup screen first (after credential reset). */
bool wifiShowsSetupScreenOnBoot();
void wifiResetCredentialsAndReboot();
/** STA connect with secrets.h before displays take RAM. */
bool wifiConnectEarly();
/** Boot flow: connect with UI, open portal only if saved creds fail. */
bool wifiSetupConnect();
/** Reconnect using saved creds; never opens the captive portal. */
bool wifiReconnect();
/** Keeps the LAN config portal alive; call every loop() iteration. */
void wifiLoop();
/** True once after portal settings are saved (location/range/sweep/etc). */
bool wifiConsumeSettingsChanged();
bool wifiBootButtonPressed();
/** GPIO + interrupt setup; call once early in setup(). */
void bootButtonInit();
/** Latched short tap (survives blocking HTTP/display work). */
bool bootButtonConsumeTap();
/** Latched long-hold range cycle (BOOT / touch). */
bool bootButtonConsumeLongRange();
bool touchButtonConsumeLongRange();
/** True if this touch long-hold should undo a press-triggered theme change. */
bool touchButtonConsumeThemeUndo();
/** Call each loop iteration; long hold cycles range, longer BOOT hold resets Wi‑Fi. */
void bootButtonPollLongPress();
/** Capacitive touch long-hold poll (range); call with bootButtonPollLongPress. */
void touchButtonPollLongPress();

/** Capacitive touch on kTouchPin — short press cycles theme. */
void touchButtonInit();
bool touchButtonConsumeTap();
/** Raw touch pad currently pressed. */
bool touchButtonIsDown();
