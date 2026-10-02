#include "state.h"
#include <config.h>

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;

LightState currentLightState = OFF;
LightState previousLightState = OFF;
unsigned long previousMillis = 0;
unsigned long currentDelay = 0;
unsigned long LED_delay_red = 0;
unsigned long LED_delay_yellow = 0;
unsigned long LED_delay_green = 0;
bool lightMode = false;
bool themeMode = false;
bool blinkState = false;
bool blinkAllColors = true; // Default to blinking all colors drop down menu
bool randomBlinkMode = false;
int blinkPin = -1;
unsigned long lastBlinkMillis = 0;

PedState currentPedState = PED_OFF;
unsigned long ped_walk_duration = 7000;  // variable, safe to change at any time
unsigned long ped_walk_effective = 7000; // walk length after trimming to fit the phase
unsigned long ped_fdw_duration = 15000;  // fixed, this is the countdown value
unsigned long ped_dw_duration = 3000;    // steady rest / minimum before the phase ends
bool ped_chained = true;                 // follow the vehicle cycle vs manual control
LightState ped_chain_phase = RED;        // vehicle phase the WALK runs under
unsigned long pedPhaseStart = 0;
unsigned long pedBlinkPrevious = 0;
bool pedBlinkState = false;

// default distances for the traffic light, get overwritten by the web interface
float distance_max = 0;
float distance_warning = 0;
float distance_danger = 0;
int zone_persistence = 3; // consecutive readings required before changing zone
bool distance_sensor_enabled = false;

bool testMode = false;
