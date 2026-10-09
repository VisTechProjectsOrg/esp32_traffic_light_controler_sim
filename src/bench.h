#pragma once

#include <Arduino.h>

// Bench testing: hold individual relay channels on, off or flashing while the normal
// cycle is suspended. Shared by the HTTP test endpoints and the serial console so
// both drive the same held state.

// Suspends the cycle with everything dark, or hands the outputs back to it.
void setTestMode(bool enabled);

// Holds one output on or off. Names: red, yellow, green, dont_walk, walk,
// cd_dont_walk, cd_walk, all_off. Returns false for an unknown name.
bool setTestOutput(const String &output, bool state);

// Whether an output is currently held on (a flashing output counts as on).
bool testOutputHeld(const String &output);

// Serial console. Type "help" for the commands. Reads a character at a time so a
// half-typed line never holds the loop up.
void pollSerialConsole();

// Keeps flashing outputs flashing. Call every pass while test mode is on.
void updateTestFlash(unsigned long now);
