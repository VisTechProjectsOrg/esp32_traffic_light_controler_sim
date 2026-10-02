#include "signals.h"
#include "state.h"
#include <config.h>

#ifdef RGB_LED_ENABLED
#include <FastLED.h>
CRGB rgbLed[1];

void setRgbLedColor(bool red, bool yellow, bool green)
{
  if (red)
    rgbLed[0] = CRGB(255, 0, 0);
  else if (yellow)
    rgbLed[0] = CRGB(255, 180, 0);
  else if (green)
    rgbLed[0] = CRGB(0, 255, 0);
  else
    rgbLed[0] = CRGB(0, 0, 0);
  FastLED.show();
}
#endif

void setupSignalPins()
{
  pinMode(LED_red_pin, OUTPUT);
  pinMode(LED_yellow_pin, OUTPUT);
  pinMode(LED_green_pin, OUTPUT);
  set_traffic_light(0, 0, 0);

#ifdef PED_SIGNAL_ENABLED
  pinMode(PED_walk_pin, OUTPUT);
  pinMode(PED_dont_walk_pin, OUTPUT);
  pinMode(PED_countdown_walk_pin, OUTPUT);
  pinMode(PED_countdown_dont_walk_pin, OUTPUT);
  set_ped_signal(0, 0);
  set_countdown_signal(0, 0);
#endif

#ifdef RGB_LED_ENABLED
  FastLED.addLeds<WS2812, RGB_LED_PIN, GRB>(rgbLed, 1);
  FastLED.setBrightness(RGB_LED_BRIGHTNESS);
#endif
}

void set_traffic_light(boolean LED_red_state, boolean LED_yellow_state, boolean LED_green_state)
{
  // output and invert the logic here for relays
  digitalWrite(LED_red_pin, !LED_red_state);
  digitalWrite(LED_yellow_pin, !LED_yellow_state);
  digitalWrite(LED_green_pin, !LED_green_state);

#ifdef RGB_LED_ENABLED
  setRgbLedColor(LED_red_state, LED_yellow_state, LED_green_state);
#endif

  String state = "all_off";
  if (LED_red_state)
    state = "red";
  else if (LED_yellow_state)
    state = "yellow";
  else if (LED_green_state)
    state = "green";

  // Send the state to the client, normal mode or cat mode
  String jsonResponse = "{\"state\":\"" + state + (themeMode ? "_cat" : "") + "\"}";
  ws.textAll(jsonResponse);
}

#ifdef PED_SIGNAL_ENABLED
void set_ped_signal(boolean walk_state, boolean dont_walk_state)
{
  // The combo module drives one symbol at a time. Energizing both hots lights both
  // symbols and draws 15W, so refuse it rather than pass it to the relays.
  if (walk_state && dont_walk_state)
    dont_walk_state = false;

  digitalWrite(PED_walk_pin, !walk_state);
  digitalWrite(PED_dont_walk_pin, !dont_walk_state);
}

void set_countdown_signal(boolean walk_state, boolean dont_walk_state)
{
  if (walk_state && dont_walk_state)
    dont_walk_state = false;

  digitalWrite(PED_countdown_walk_pin, !walk_state);
  digitalWrite(PED_countdown_dont_walk_pin, !dont_walk_state);
}
#endif
