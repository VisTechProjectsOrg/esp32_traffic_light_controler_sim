#include "traffic.h"
#include "signals.h"
#include "ped.h"
#include <config.h>

void randomBlink()
{
  // Randomly select a pin to blink
  int randomColor = random(3); // Generates a random number from 0 to 2 (3 colors)

  if (randomColor == 0)
  {
    blinkPin = LED_red_pin; // Red
  }
  else if (randomColor == 1)
  {
    blinkPin = LED_yellow_pin; // Yellow
  }
  else
  {
    blinkPin = LED_green_pin; // Green
  }

  blinkAllColors = false;     // Disable blinking all colors, since we're doing a single color
  blinkState = false;         // Reset blink state
  lastBlinkMillis = millis(); // Reset blink timer
}

void cycleLights()
{
  if (testMode)
    return;

  unsigned long currentMillis = millis();
  if (lightMode)
  {
    if (currentMillis - lastBlinkMillis >= blinkInterval)
    {
      lastBlinkMillis = currentMillis;
      blinkState = !blinkState;
      if (blinkAllColors)
      {
        digitalWrite(LED_red_pin, !blinkState); // Invert all the output state
        digitalWrite(LED_yellow_pin, !blinkState);
        digitalWrite(LED_green_pin, !blinkState);
#ifdef RGB_LED_ENABLED
        setRgbLedColor(blinkState, blinkState, blinkState);
#endif
      }
      else
      {
        if (randomBlinkMode)
        {
          int randomColor = random(3);

          if (randomColor == 0)
          {
            blinkPin = LED_red_pin;
          }
          else if (randomColor == 1)
          {
            blinkPin = LED_yellow_pin;
          }
          else
          {
            blinkPin = LED_green_pin;
          }

          blinkAllColors = false;
          blinkState = true;

          digitalWrite(blinkPin, !blinkState);
#ifdef RGB_LED_ENABLED
          setRgbLedColor(blinkPin == LED_red_pin, blinkPin == LED_yellow_pin, blinkPin == LED_green_pin);
#endif
        }
        else
        {
          digitalWrite(blinkPin, !blinkState);
#ifdef RGB_LED_ENABLED
          if (blinkState)
            setRgbLedColor(blinkPin == LED_red_pin, blinkPin == LED_yellow_pin, blinkPin == LED_green_pin);
          else
            setRgbLedColor(false, false, false);
#endif
        }
      }

      // Update the traffic light image on the webpage
      String state = "all_off";
      if (blinkState)
      {
        if (blinkAllColors)
          state = "all_on";
        else if (blinkPin == LED_red_pin)
          state = "red";
        else if (blinkPin == LED_yellow_pin)
          state = "yellow";
        else if (blinkPin == LED_green_pin)
          state = "green";
      }

      if (themeMode) // return either the cat light or nomal traffic light
      {
        String jsonResponse = "{\"state\":\"" + state + "_cat\"}";
        ws.textAll(jsonResponse);
      }
      else
      {
        String jsonResponse = "{\"state\":\"" + state + "\"}";
        ws.textAll(jsonResponse);
      }
    }
    return;
  }

  if (currentMillis - previousMillis >= currentDelay) // only change the light stated after a timmed delay
  {

    // Serial.print("Switching to ");
    // Serial.print(currentLightState);
    // Serial.print(" with delay: ");
    // Serial.println(currentDelay);

    previousMillis = currentMillis;
    previousLightState = currentLightState; // Update previous state immediately

    switch (currentLightState)
    {
    case RED: // if red turn it to green
      currentLightState = GREEN;
      currentDelay = LED_delay_green;
      break;
    case GREEN: // if green turn it to yellow
      currentLightState = YELLOW;
      currentDelay = LED_delay_yellow;
      break;
    case YELLOW: // if yellow turn it to red
      currentLightState = RED;
      currentDelay = LED_delay_red;
      break;
    case OFF: // if off turn it to red?
      currentLightState = RED;
      currentDelay = LED_delay_red;
      break;
    }
  }

  if (currentLightState != previousLightState) // if the light state has changed, update the light output
  {
    switch (currentLightState)
    {
    case RED:
      set_traffic_light(1, 0, 0);
      break;
    case GREEN:
      set_traffic_light(0, 0, 1);
      break;
    case YELLOW:
      set_traffic_light(0, 1, 0);
      break;
    case OFF:
      set_traffic_light(1, 1, 1);
      break;
    }
    previousLightState = currentLightState;

#ifdef PED_SIGNAL_ENABLED
    if (ped_chained)
    {
      if (currentLightState == ped_chain_phase)
        startPedPhase(currentDelay);
      else if (currentPedState != PED_DONT_WALK)
        setPedState(PED_DONT_WALK);
    }
#endif
  }
}
