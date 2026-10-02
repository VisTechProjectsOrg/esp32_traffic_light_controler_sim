#pragma once

#include <Arduino.h>
#include <config.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

// Shared state for the controller. The subsystems (traffic, ped, proximity, web)
// read and write these; each owns the ones documented as its own.

enum LightState
{
  RED,
  GREEN,
  YELLOW,
  OFF
};

// Pedestrian phase. WALK length is a traffic-flow number and may vary; FDW is the
// clearance interval and must stay constant, because its length is literally the
// number the countdown module learns and displays.
enum PedState
{
  PED_WALK,
  PED_FDW,
  PED_DONT_WALK,
  PED_OFF
};

extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern Preferences preferences;

// --- traffic (owned by traffic.cpp) ---
extern LightState currentLightState;
extern LightState previousLightState;
extern unsigned long previousMillis;
extern unsigned long currentDelay;
extern unsigned long LED_delay_red;
extern unsigned long LED_delay_yellow;
extern unsigned long LED_delay_green;
extern bool lightMode;
extern bool themeMode;
extern bool blinkState;
extern bool blinkAllColors;
extern bool randomBlinkMode;
extern int blinkPin;
extern unsigned long lastBlinkMillis;

// --- pedestrian (owned by ped.cpp) ---
extern PedState currentPedState;
extern unsigned long ped_walk_duration;
extern unsigned long ped_walk_effective;
extern unsigned long ped_fdw_duration;
extern bool ped_chained;
extern LightState ped_chain_phase;
extern unsigned long pedPhaseStart;
extern unsigned long pedBlinkPrevious;
extern bool pedBlinkState;

// --- proximity (owned by proximity.cpp) ---
extern float distance_max;
extern float distance_warning;
extern float distance_danger;
extern int zone_persistence;
extern bool distance_sensor_enabled;

// Bench test mode: suspends the automatic cycle so the test GUI can assert
// individual relay channels without the cycle overwriting them.
extern bool testMode;
