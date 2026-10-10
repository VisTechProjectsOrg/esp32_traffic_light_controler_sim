#pragma once

#include <Arduino.h>
#include <config.h>

// Every relay output goes through this layer. The board is low-level trigger, so
// the inversion lives here and nowhere else - writing a pin directly elsewhere is
// how the lights end up on when they should be off.

void setupSignalPins();

// Power-up lamp test: each vehicle lamp, then the hand and the walk, one at a time.
// Blocks for a couple of seconds, so call it from setup() only.
void runLampTest();

void set_traffic_light(boolean LED_red_state, boolean LED_yellow_state, boolean LED_green_state);

#ifdef RGB_LED_ENABLED
void setRgbLedColor(bool red, bool yellow, bool green);
#endif

#ifdef PED_SIGNAL_ENABLED
// The hand/man combo head.
void set_ped_signal(boolean walk_state, boolean dont_walk_state);

// The countdown module, on its own pair of channels. Kept separate from the combo
// head on purpose: proximity mode may flash the hand symbol for reasons that have
// nothing to do with a pedestrian phase, and the countdown must not see that or it
// will relearn a bogus clearance interval. Separate channels also leave the door
// open to timing it independently of the combo head.
void set_countdown_signal(boolean walk_state, boolean dont_walk_state);
#endif
