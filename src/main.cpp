#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <config.h>
#include <ArduinoJson.h>
#include "WiFiManager.h"
#include <WebSocketsServer.h>
#include <ota_updater.h>
#include <version.h>

#ifdef DISTANCE_SENSOR_ENABLED
#include <LidarHelper.h>
#endif

#ifdef RGB_LED_ENABLED
#include <FastLED.h>
CRGB rgbLed[1];
#endif

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;

// **** ANY VARIBLE CHANGES, MODIFY THE CONFIG.H FILE ****
// **** Remeber to build and upload all SPIFFS files!!! ****

enum LightState
{
  RED,
  GREEN,
  YELLOW,
  OFF
};

// default settings for the traffic light
LightState currentLightState = OFF;
LightState previousLightState = OFF;

unsigned long previousMillis = 0;
unsigned long currentDelay = 0;

unsigned long LED_delay_red = 0;
unsigned long LED_delay_yellow = 0;
unsigned long LED_delay_green = 0;

// default delays for the traffic light, get overwritten by the web interface
float distance_max = 0;
float distance_warning = 0;
float distance_danger = 0;
int zone_persistence = 3;  // consecutive readings required before changing zone

bool distance_sensor_enabled = false; // Default value false

bool lightMode = false;
bool themeMode = false;
bool blinkState = false;
bool blinkAllColors = true; // Default to blinking all colors drop down menu
bool randomBlinkMode = false;

int blinkPin = -1;

unsigned long lastBlinkMillis = 0;

// Bench test mode: suspends the automatic cycle so the test GUI can assert
// individual relay channels without the cycle overwriting them.
bool testMode = false;

#ifdef PED_SIGNAL_ENABLED
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

PedState currentPedState = PED_OFF;

unsigned long ped_walk_duration = 7000;  // variable, safe to change at any time
unsigned long ped_walk_effective = 7000; // walk length after trimming to fit the phase
unsigned long ped_fdw_duration = 15000;  // fixed, this is the countdown value
bool ped_chained = true;                 // follow the vehicle cycle vs manual control
LightState ped_chain_phase = RED;        // vehicle phase the WALK runs under

unsigned long pedPhaseStart = 0;
unsigned long pedBlinkPrevious = 0;
bool pedBlinkState = false;
#endif

#ifdef RGB_LED_ENABLED
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
  else
    state = "all_off";

  // Send the state to the client nomalt mode/cat mode
  if (themeMode)
  {
    // Serial.println("Cat Mode: " + state);
    String jsonResponse = "{\"state\":\"" + state + "_cat\"}";
    ws.textAll(jsonResponse);
  }
  else
  {
    // Serial.println("Normal Mode: " + state);
    String jsonResponse = "{\"state\":\"" + state + "\"}";
    ws.textAll(jsonResponse);
  }
}

#ifdef PED_SIGNAL_ENABLED
const char *pedStateName(PedState state)
{
  switch (state)
  {
  case PED_WALK:
    return "walk";
  case PED_FDW:
    return "fdw";
  case PED_DONT_WALK:
    return "dont_walk";
  default:
    return "off";
  }
}

void set_ped_signal(boolean walk_state, boolean dont_walk_state)
{
  // The combo module drives one symbol at a time. Energizing both hots lights both
  // symbols and draws 15W, so refuse it rather than pass it to the relays.
  if (walk_state && dont_walk_state)
    dont_walk_state = false;

  // output and invert the logic here for relays
  digitalWrite(PED_walk_pin, !walk_state);
  digitalWrite(PED_dont_walk_pin, !dont_walk_state);
}

void setPedState(PedState state)
{
  currentPedState = state;
  pedPhaseStart = millis();

  switch (state)
  {
  case PED_WALK:
    set_ped_signal(true, false);
    break;

  case PED_FDW:
    // FDW starts lit, then chops at 1Hz. The flashing is what tells the
    // countdown module to start counting.
    pedBlinkState = true;
    pedBlinkPrevious = pedPhaseStart;
    set_ped_signal(false, true);
    break;

  case PED_DONT_WALK:
    set_ped_signal(false, true);
    break;

  case PED_OFF:
    set_ped_signal(false, false);
    break;
  }

  String jsonResponse = "{\"ped_state\":\"" + String(pedStateName(state)) + "\"}";
  ws.textAll(jsonResponse);
}

// Start a ped phase, trimming WALK so the whole thing fits inside the given vehicle
// phase. FDW is never trimmed - a short clearance interval would teach the countdown
// module the wrong number - so if even FDW alone does not fit, the phase is skipped.
void startPedPhase(unsigned long availableTime)
{
  if (availableTime < ped_fdw_duration)
  {
    Serial.println("Ped phase skipped: vehicle phase (" + String(availableTime) +
                   "ms) shorter than FDW (" + String(ped_fdw_duration) + "ms)");
    setPedState(PED_DONT_WALK);
    return;
  }

  unsigned long room = availableTime - ped_fdw_duration;
  ped_walk_effective = ped_walk_duration;
  if (ped_walk_effective > room)
  {
    ped_walk_effective = room;
    Serial.println("Ped WALK trimmed to " + String(room) + "ms to fit the vehicle phase");
  }

  setPedState(PED_WALK);
}

void updatePedSignal(unsigned long currentMillis)
{
  unsigned long elapsed = currentMillis - pedPhaseStart;

  switch (currentPedState)
  {
  case PED_WALK:
    if (elapsed >= ped_walk_effective)
      setPedState(PED_FDW);
    break;

  case PED_FDW:
    if (currentMillis - pedBlinkPrevious >= pedFdwFlashInterval)
    {
      pedBlinkPrevious = currentMillis;
      pedBlinkState = !pedBlinkState;
      set_ped_signal(false, pedBlinkState);
    }
    if (elapsed >= ped_fdw_duration)
      setPedState(PED_DONT_WALK);
    break;

  default:
    break;
  }
}
#endif

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

void notifyAllClients(String message)
{
  for (int i = 0; i < ws.count(); i++)
  {
    ws.text(i, message);
  }
}

void handleRoot(AsyncWebServerRequest *request)
{
  IPAddress requesterIP = request->client()->remoteIP();

  // Get the User-Agent (if present)
  String userAgent = request->header("User-Agent");

  Serial.print("Got root request from IP: ");
  Serial.println(requesterIP);

  Serial.print("User-Agent: ");
  Serial.println(userAgent);

  otaPageActive = false;

  if (SPIFFS.exists("/index.html"))
  {
    cycleLights();
    request->send(SPIFFS, "/index.html", "text/html; charset=utf-8");
  }
  else
  {
    Serial.println("index.html not found");
    request->send(SPIFFS, "/index_page_not_found.html", "text/html; charset=utf-8");
  }
}

String getSpiffsVersion() {
  File file = SPIFFS.open("/version.txt", "r");
  if (!file) return "0.0";
  String version = file.readStringUntil('\n');
  file.close();
  version.trim();
  return version;
}

void handleGetConfig(AsyncWebServerRequest *request)
{
  Serial.println("Sending get config");

  String jsonResponse = "{\"delay_red\":" + String(LED_delay_red / 1000) +
                        ",\"delay_yellow\":" + String(LED_delay_yellow / 1000) +
                        ",\"delay_green\":" + String(LED_delay_green / 1000) +
                        ",\"distance_max\":" + String(distance_max) +
                        ",\"distance_warning\":" + String(distance_warning) +
                        ",\"distance_danger\":" + String(distance_danger) +
                        ",\"zone_persistence\":" + String(zone_persistence) +
                        ",\"distance_sensor_enabled\":" + String(distance_sensor_enabled ? "true" : "false") +
                        ",\"version_firmware\":\"" + String(VERSION_FIRMWARE) + "\"" +
                        ",\"version_spiffs\":\"" + getSpiffsVersion() + "\"}";

  request->send(200, "application/json", jsonResponse);
}

void handleFormConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, (const char *)data);

  if (error)
  {
    Serial.println("Failed to parse JSON");
    request->send(400, "application/json", "{\"error\": \"Invalid JSON\"}");
    return;
  }

  String action = doc["action"];

  if (action == "set_config")
  {
    Serial.println("Setting config values...");

    LED_delay_red = doc["delay_red"].as<unsigned long>() * 1000; // Convert seconds to milliseconds
    LED_delay_yellow = doc["delay_yellow"].as<unsigned long>() * 1000;
    LED_delay_green = doc["delay_green"].as<unsigned long>() * 1000;
    distance_max = doc["distance_max"].as<float>();
    distance_warning = doc["distance_warning"].as<float>();
    distance_danger = doc["distance_danger"].as<float>();
    zone_persistence = doc["zone_persistence"].as<int>();
    distance_sensor_enabled = doc["distance_sensor_enabled"].as<bool>();

    Serial.println("Action: " + action);
    Serial.println("delay_red: " + String(LED_delay_red));
    Serial.println("delay_yellow: " + String(LED_delay_yellow));
    Serial.println("delay_green: " + String(LED_delay_green));
    Serial.println("distance_max: " + String(distance_max));
    Serial.println("distance_warning: " + String(distance_warning));
    Serial.println("distance_danger: " + String(distance_danger));
    Serial.println("zone_persistence: " + String(zone_persistence));
    Serial.println("distance_sensor_enabled: " + String(distance_sensor_enabled));

    // Convert float to unsigned long for storage in Preferences

    // Save to Preferences
    preferences.putULong("delay_red", LED_delay_red);
    preferences.putULong("delay_yellow", LED_delay_yellow);
    preferences.putULong("delay_green", LED_delay_green);
    preferences.putFloat("dist_max", distance_max);
    preferences.putFloat("dist_warn", distance_warning);
    preferences.putFloat("dist_dang", distance_danger);
    preferences.putInt("zone_persist", zone_persistence);
    preferences.putBool("dist_sens_en", distance_sensor_enabled);

    JsonDocument responseDoc;
    responseDoc["message"] = "Config updated!";
    responseDoc["delay_red"] = preferences.getULong("delay_red", -1);
    responseDoc["delay_yellow"] = preferences.getULong("delay_yellow", -1);
    responseDoc["delay_green"] = preferences.getULong("delay_green", -1);
    responseDoc["distance_max"] = preferences.getFloat("dist_max", -1);
    responseDoc["distance_warning"] = preferences.getFloat("dist_warn", -1);
    responseDoc["distance_danger"] = preferences.getFloat("dist_dang", -1);
    responseDoc["zone_persistence"] = preferences.getInt("zone_persist", 3);
    responseDoc["distance_sensor_enabled"] = preferences.getBool("dist_sens_en", false);

    String message;
    serializeJson(responseDoc, message);
    request->send(200, "application/json", message);
  }
  else if (action == "reset_values")
  {
    Serial.println("Resetting values... to be implemented");
    request->send(200, "application/json", "{\"message\": \"Values reset! to be implented...\"}");
  }
  else
  {
    request->send(400, "application/json", "{\"error\": \"Invalid action - " + String(action) + "\"}");
  }
}

void handleGetCurrentState(AsyncWebServerRequest *request)
{
  Serial.println("Sending current state");

  String mode = lightMode ? "blink_mode" : "cycle_mode";
  String theme = themeMode ? "cat_mode" : "normal_mode";

  String state = "all_off";
  if (currentLightState == RED)
    state = "red";
  else if (currentLightState == YELLOW)
    state = "yellow";
  else if (currentLightState == GREEN)
    state = "green";

  if (themeMode)
  {
    state += "_cat"; // Append "_cat" if cat mode is enabled
  }

  String blinkColor = "none";
  if (blinkPin == LED_red_pin)
    blinkColor = "red";
  else if (blinkPin == LED_yellow_pin)
    blinkColor = "yellow";
  else if (blinkPin == LED_green_pin)
    blinkColor = "green";

  if (blinkAllColors)
    blinkColor = "all";

  if (randomBlinkMode)
    blinkColor = "random";

  String jsonResponse = "{\"light_mode\":\"" + mode + "\","
                                                      "\"theme_mode\":\"" +
                        theme + "\","
                                "\"state\":\"" +
                        state + "\","
                                "\"blink_color\":\"" +
                        blinkColor + "\"}";

  request->send(200, "application/json", jsonResponse);
}

void handlelightMode(AsyncWebServerRequest *request)
{
  if (request->hasParam("color"))
  {
    String color = request->getParam("color")->value();
    if (color == "red")
    {
      blinkPin = LED_red_pin;
      blinkAllColors = false;
      randomBlinkMode = false;
    }
    else if (color == "yellow")
    {
      blinkPin = LED_yellow_pin;
      blinkAllColors = false;
      randomBlinkMode = false;
    }
    else if (color == "green")
    {
      blinkPin = LED_green_pin;
      blinkAllColors = false;
      randomBlinkMode = false;
    }
    else if (color == "all")
    {
      blinkAllColors = true;
      randomBlinkMode = false;
    }
    else if (color == "random")
    {
      Serial.println("Random Blink Mode");
      blinkAllColors = false;
      randomBlinkMode = true;
    }
    else
    {
      request->send(400, "text/plain", "Invalid Color 1: " + color);
      return;
    }

    lightMode = true;
    blinkState = false;
    lastBlinkMillis = millis();

    String jsonResponse = "{\"light_mode\":\"blink_mode\"}";
    ws.textAll(jsonResponse); // Send update to all clients

    jsonResponse = "{\"blink_color\":\"" + color + "\"}";
    ws.textAll(jsonResponse); // Send update to all clients

    request->send(200, "text/plain", "Blink Mode Set: " + color);
  }
  else
  {
    request->send(400, "text/plain", "Invalid Color 2: invalid param");
  }
}

void handleToggleLightMode(AsyncWebServerRequest *request)
{
  Serial.println("Toggling handleToggleLightMode");

  lightMode = !lightMode;
  preferences.putBool("lightMode", lightMode); // Save to flash

  if (!lightMode)
  {
    set_traffic_light(0, 0, 0); // relays are active low, write through the helper
    blinkPin = -1;
  }
  else
  {
    blinkAllColors = true; // Default to blinking all colors
  }

  Serial.println("Toggling " + String(lightMode ? "Blink Mode" : "Cycle Mode"));

  // Send update to all clients
  String jsonResponse = "{\"light_mode\":\"" + String(lightMode ? "blink_mode" : "cycle_mode") + "\"}";
  ws.textAll(jsonResponse); // Send update to all clients

  request->send(200, "text/plain", lightMode ? "Blink Mode Enabled" : "Cycle Mode Enabled");
}

void handleToggleThemeMode(AsyncWebServerRequest *request)
{
  themeMode = !themeMode;
  preferences.putBool("themeMode", themeMode);

  Serial.println("Toggling handleToggleThemeMode: " + String(themeMode ? "cat mode" : "normal mode"));

  String jsonResponse = "{\"theme_mode\":\"" + String(themeMode ? "cat_mode" : "normal_mode") + "\"}";
  ws.textAll(jsonResponse); // Send update to all clients

  request->send(200, "text/plain", themeMode ? "Cat mode enabled" : "normal mode enabled");
}

void handleFirmwareUpdateStateReset(AsyncWebServerRequest *request)
{
  otaPageActive = false;
  Serial.println("OTA Page Active reset to false");
  request->send(200, "application/json", "{\"message\": \"OTA state reset\"}");
}

void notifyAllClientsDistance(float distance, int16_t &outTemp)
{
  String jsonResponse;
  if (distance == -1)
  {
    jsonResponse = "{\"distance\":null,\"sensor_temp\":" + String(outTemp) + "}";
  }
  else
  {
    jsonResponse = "{\"distance\":" + String(distance) + ",\"sensor_temp\":" + String(outTemp) + "}";
  }
  ws.textAll(jsonResponse);
  // Serial.println("Distance: " + String(distance) + " FT, Temp: " + String(outTemp) + " C | notify all
}

#ifdef PED_SIGNAL_ENABLED
// Bench test endpoints used by tools/signal_test_gui.py.
// /test_mode suspends the cycle, /set_output asserts one relay channel so you can
// confirm which physical wire each channel actually lights.
void handleTestMode(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  JsonDocument doc;
  if (deserializeJson(doc, (const char *)data))
  {
    request->send(400, "application/json", "{\"error\": \"Invalid JSON\"}");
    return;
  }

  testMode = doc["enabled"].as<bool>();
  Serial.println("Test mode: " + String(testMode ? "on" : "off"));

  if (testMode)
  {
    // everything dark so the operator starts from a known state
    set_traffic_light(0, 0, 0);
    set_ped_signal(0, 0);
  }
  else
  {
    previousLightState = OFF;
    currentLightState = OFF;
    setPedState(PED_DONT_WALK);
  }

  request->send(200, "application/json", "{\"test_mode\":" + String(testMode ? "true" : "false") + "}");
}

void handleSetOutput(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  JsonDocument doc;
  if (deserializeJson(doc, (const char *)data))
  {
    request->send(400, "application/json", "{\"error\": \"Invalid JSON\"}");
    return;
  }

  if (!testMode)
  {
    request->send(409, "application/json", "{\"error\": \"Enable test mode first\"}");
    return;
  }

  String output = doc["output"].as<String>();
  bool state = doc["state"].as<bool>();

  // Held outputs, so the GUI can light several channels at once while ringing out wires.
  static bool red = false, yellow = false, green = false, walk = false, dontWalk = false;

  if (output == "red")
    red = state;
  else if (output == "yellow")
    yellow = state;
  else if (output == "green")
    green = state;
  else if (output == "walk")
    walk = state;
  else if (output == "dont_walk")
    dontWalk = state;
  else if (output == "all_off")
    red = yellow = green = walk = dontWalk = false;
  else
  {
    request->send(400, "application/json", "{\"error\": \"Unknown output\"}");
    return;
  }

  set_traffic_light(red, yellow, green);
  set_ped_signal(walk, dontWalk);

  JsonDocument out;
  out["red"] = red;
  out["yellow"] = yellow;
  out["green"] = green;
  out["walk"] = walk;
  out["dont_walk"] = dontWalk;
  String body;
  serializeJson(out, body);
  request->send(200, "application/json", body);
}

void handlePedControl(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  JsonDocument doc;
  if (deserializeJson(doc, (const char *)data))
  {
    request->send(400, "application/json", "{\"error\": \"Invalid JSON\"}");
    return;
  }

  String action = doc["action"].as<String>();

  if (action == "set_state")
  {
    String state = doc["state"].as<String>();
    if (state == "walk")
      setPedState(PED_WALK);
    else if (state == "fdw")
      setPedState(PED_FDW);
    else if (state == "dont_walk")
      setPedState(PED_DONT_WALK);
    else if (state == "off")
      setPedState(PED_OFF);
    else
    {
      request->send(400, "application/json", "{\"error\": \"Unknown ped state\"}");
      return;
    }
  }
  else if (action == "set_config")
  {
    ped_walk_duration = doc["walk"].as<unsigned long>() * 1000;
    ped_fdw_duration = doc["fdw"].as<unsigned long>() * 1000;
    ped_chained = doc["chained"].as<bool>();
    ped_chain_phase = doc["chain_phase"].as<String>() == "green" ? GREEN : RED;
    ped_walk_effective = ped_walk_duration;

    preferences.putULong("ped_walk", ped_walk_duration);
    preferences.putULong("ped_fdw", ped_fdw_duration);
    preferences.putBool("ped_chained", ped_chained);
    preferences.putInt("ped_chain_ph", ped_chain_phase);

    Serial.println("Ped config: walk=" + String(ped_walk_duration) +
                   "ms fdw=" + String(ped_fdw_duration) +
                   "ms chained=" + String(ped_chained));
  }

  JsonDocument out;
  out["ped_state"] = pedStateName(currentPedState);
  out["walk"] = ped_walk_duration / 1000;
  out["fdw"] = ped_fdw_duration / 1000;
  out["chained"] = ped_chained;
  out["chain_phase"] = ped_chain_phase == GREEN ? "green" : "red";
  out["test_mode"] = testMode;
  String body;
  serializeJson(out, body);
  request->send(200, "application/json", body);
}
#endif

void listSPIFFSFiles()
{
  Serial.println("Listing SPIFFS files:");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file)
  {
    Serial.print("FILE: ");
    Serial.print(file.name());
    Serial.print("\tSIZE: ");
    Serial.println(file.size());
    file = root.openNextFile();
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("\nStarting");

  pinMode(LED_red_pin, OUTPUT);
  pinMode(LED_yellow_pin, OUTPUT);
  pinMode(LED_green_pin, OUTPUT);

  set_traffic_light(0, 0, 0);

#ifdef PED_SIGNAL_ENABLED
  pinMode(PED_walk_pin, OUTPUT);
  pinMode(PED_dont_walk_pin, OUTPUT);
  set_ped_signal(0, 0);
#endif

#ifdef DISTANCE_SENSOR_ENABLED
  setupLidar();
#endif

#ifdef RGB_LED_ENABLED
  FastLED.addLeds<WS2812, RGB_LED_PIN, GRB>(rgbLed, 1);
  FastLED.setBrightness(50);
#endif

// 1) Prevent ambiguous dual‑modes:
#if defined(WIFI_SSID) && defined(WIFI_PASS) && defined(AP_SSID)
#error "You cannot define both WIFI_SSID/PASS and AP_SSID (with or without AP_PASS)."
#endif

// 2) Station mode if both creds are set
#if defined(WIFI_SSID) && defined(WIFI_PASS)
  WifiManager::beginStation(WIFI_SSID, WIFI_PASS, server);
// 3) AP mode if SSID is set
#elif defined(AP_SSID)
// 3a) Secured AP if password provided
#ifdef AP_PASS
  WifiManager::beginAP(AP_SSID, AP_PASS, server);
// 3b) Open AP otherwise
#else
  WifiManager::beginAP(AP_SSID, /* open‑auth */ "", server);
#endif
// 4) Error if neither mode was configured
#else
#error "You must define either WIFI_SSID/PASS or AP_SSID (optionally AP_PASS) in config.h"
#endif

  if (!MDNS.begin("trafficlights"))
  {
    Serial.println("Error setting up MDNS responder!");
  }

  if (!SPIFFS.begin(false))
  {
    Serial.println("Failed to mount file system");
    return;
  }

  preferences.begin("traffic-light", false);

  // Set defaults for timing delays
  if (!preferences.isKey("delay_red"))
    preferences.putULong("delay_red", 5000);
  if (!preferences.isKey("delay_yellow"))
    preferences.putULong("delay_yellow", 3000);
  if (!preferences.isKey("delay_green"))
    preferences.putULong("delay_green", 6000);

  // Set defaults for distance zones (stored as float)
  if (!preferences.isKey("dist_max"))
    preferences.putFloat("dist_max", 15.0);
  if (!preferences.isKey("dist_warn"))
    preferences.putFloat("dist_warn", 5.0);
  if (!preferences.isKey("dist_dang"))
    preferences.putFloat("dist_dang", 2.0);
  if (!preferences.isKey("zone_persist"))
    preferences.putInt("zone_persist", 3);

#ifdef PED_SIGNAL_ENABLED
  if (!preferences.isKey("ped_walk"))
    preferences.putULong("ped_walk", 7000);
  if (!preferences.isKey("ped_fdw"))
    preferences.putULong("ped_fdw", 15000);
  if (!preferences.isKey("ped_chained"))
    preferences.putBool("ped_chained", true);
  if (!preferences.isKey("ped_chain_ph"))
    preferences.putInt("ped_chain_ph", RED);
#endif

  // Load timing delays
  LED_delay_red = preferences.getULong("delay_red", 5000);
  LED_delay_yellow = preferences.getULong("delay_yellow", 3000);
  LED_delay_green = preferences.getULong("delay_green", 6000);

// Distance sensor: if programaticly enabled, load status from preferences
#ifdef DISTANCE_SENSOR_ENABLED
  distance_sensor_enabled = preferences.getBool("dist_sens_en", false);
#else
  distance_sensor_enabled = false;
#endif

  Serial.println("Distance sensor enabled: " + String(distance_sensor_enabled ? "true" : "false"));

  distance_max = preferences.getFloat("dist_max", 15.0);
  distance_warning = preferences.getFloat("dist_warn", 5.0);
  distance_danger = preferences.getFloat("dist_dang", 2.0);
  zone_persistence = preferences.getInt("zone_persist", 3);

#ifdef PED_SIGNAL_ENABLED
  ped_walk_duration = preferences.getULong("ped_walk", 7000);
  ped_walk_effective = ped_walk_duration;
  ped_fdw_duration = preferences.getULong("ped_fdw", 15000);
  ped_chained = preferences.getBool("ped_chained", true);
  ped_chain_phase = (LightState)preferences.getInt("ped_chain_ph", RED);
#endif

  // listSPIFFSFiles();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/update_firmware", HTTP_GET, handleFirmwareUpdate);
  server.on("/reset_ota_state", HTTP_POST, handleFirmwareUpdateStateReset);
  server.on("/get_current_state", HTTP_GET, handleGetCurrentState);
#ifdef PED_SIGNAL_ENABLED
  server.on("/test_mode", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleTestMode);
  server.on("/set_output", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleSetOutput);
  server.on("/ped_control", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handlePedControl);
#endif
  server.on("/get_config", HTTP_GET, handleGetConfig);
  server.on("/set_config", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleFormConfig);
  server.on("/blink_mode", HTTP_GET, handlelightMode);
  server.on("/toggle_light_mode", HTTP_GET, handleToggleLightMode);
  server.on("/toggle_theme_mode", HTTP_GET, handleToggleThemeMode);

  server.serveStatic("/", SPIFFS, "/");
  server.serveStatic("/images", SPIFFS, "/images");
  server.onNotFound([](AsyncWebServerRequest *request)
                    {
    if (request->header("Accept").indexOf("application/json") != -1) {
        request->send(404, "application/json", "{\"error\":\"Not found\"}");
    } else {
        auto res = request->beginResponse(SPIFFS,"/index_page_not_found.html","text/html; charset=utf-8");
        res->setCode(404);
        request->send(res);
    } });

  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
             {
              if (type == WS_EVT_CONNECT) {
                Serial.println("WebSocket client connected");
                otaPageActive = false;
                
                // Send the current state to the client
                String state = "all_off";
                if (currentLightState == RED) state = "red";
                else if (currentLightState == YELLOW) state = "yellow";
                else if (currentLightState == GREEN) state = "green";

                String mode = lightMode ? "blink_mode" : "cycle_mode";
                String jsonResponse = "{\"light_mode\":\"" + mode + "\"}";
                client->text(jsonResponse);
                
                mode = themeMode ? "cat_mode" : "normal_mode";
                jsonResponse = "{\"theme_mode\":\"" + mode + "\"}";
                client->text(jsonResponse);
                
              } 
              else if (type == WS_EVT_DISCONNECT) {
                Serial.println("WebSocket client disconnected");
              } });

  server.addHandler(&ws);

  server.begin();
  Serial.println("HTTP server started");

  setupOTA(server); // Enable OTA route
  Serial.println("OTA setup complete");
}

void loop()
{
  // State variables for distance sensing & danger flashing
  static unsigned long lastDistanceCheck = 0;
  static unsigned long dangerStartTime = 0;
  static bool cycling = true;
  static bool hasFlashedInDanger = false;
  static bool inDangerCycleMode = false;
  static int sensorFailCount = 0;
  const int MAX_SENSOR_FAILURES = 10;

  // ⬇ New non-blocking flash state
  static bool dangerFlashing = false;
  static int flashCycles = 0;
  static bool flashOn = false;
  static unsigned long lastFlashToggle = 0;

  // Zone persistence to filter spurious readings
  static int consecutiveReadings = 0;
  static int lastZone = -1;  // -1=out of range, 0=green, 1=yellow, 2=danger

  float distance = -1;

  // —— 1) Handle non-blocking red-flash mode ——
  if (dangerFlashing)
  {
    unsigned long now = millis();
    if (now - lastFlashToggle >= 500)
    {
      lastFlashToggle = now;
      flashOn = !flashOn;
      // toggle red LED
      set_traffic_light(flashOn, 0, 0);

      // count full on/off cycles
      if (!flashOn)
      {
        flashCycles++;
        if (flashCycles >= 3)
        {
          // done flashing → enter danger cycle mode
          dangerFlashing = false;
          inDangerCycleMode = true;
          cycling = true;
        }
      }
    }
    // continue to read sensor below, but don't change lights
  }

  // —— 2) Distance-sensor logic ——
  if (distance_sensor_enabled && !otaPageActive) // Only check distance if sensor is enabled and OTA page is not active
  {
    // only check twice per second
    if (millis() - lastDistanceCheck >= 500)
    {
      lastDistanceCheck = millis();

      int16_t distance_cm, strength, temp;

      bool sensorOk = getOptimalMeasurement(distance_cm, distance, strength, temp);

      if (!sensorOk)
      {
        sensorFailCount++;
        Serial.printf("Sensor read failed (%d/%d)\n", sensorFailCount, MAX_SENSOR_FAILURES);

        if (sensorFailCount >= MAX_SENSOR_FAILURES)
        {
          Serial.println("Sensor disconnected - disabling for this session");
          distance_sensor_enabled = false;
          ws.textAll("{\"sensor_disconnected\":true}");
        }
        cycleLights();
        return;
      }

      sensorFailCount = 0; // reset on successful read

      Serial.printf("Distance: %d cm, %.2f ft, Strength: %d, Temp: %d C\n", distance_cm, distance, strength, temp);

      notifyAllClientsDistance(distance, temp);

      // Determine current zone
      int currentZone;
      if (distance == -1 || distance >= distance_max) {
        currentZone = -1;  // out of range
      } else if (distance > distance_warning) {
        currentZone = 0;   // green zone
      } else if (distance > distance_danger) {
        currentZone = 1;   // yellow zone
      } else {
        currentZone = 2;   // danger zone
      }

      // Zone persistence check
      if (currentZone != lastZone) {
        consecutiveReadings = 1;
        lastZone = currentZone;
        Serial.printf("[PERSIST] Zone changed to %d, need %d readings\n", currentZone, zone_persistence);
      } else {
        consecutiveReadings++;
      }

      // Skip light changes until we have enough consecutive readings
      if (consecutiveReadings < zone_persistence) {
        Serial.printf("[PERSIST] Zone %d: %d/%d readings\n", currentZone, consecutiveReadings, zone_persistence);
        return;
      }

      // start timing how long we've been in the danger zone
      if (distance != -1 && distance <= distance_danger)
      {
        if (dangerStartTime == 0 && !hasFlashedInDanger)
        {
          dangerStartTime = millis();
        }
      }
      else
      {
        // reset when out of danger
        dangerStartTime = 0;
        hasFlashedInDanger = false;
        inDangerCycleMode = false;
        cycling = false;
      }

      // Skip light control if currently flashing
      if (!dangerFlashing)
      {
        // — Out of sensor range or beyond max → normal cycle
        if (distance == -1 || distance >= distance_max)
        {
          cycling = true;
          inDangerCycleMode = false;
          cycleLights();
        }
        // — Just-entered danger held long enough? start flashing red light
        else if (dangerStartTime > 0 && millis() - dangerStartTime >= dangerHoldTime && !hasFlashedInDanger)
        {
          hasFlashedInDanger = true;
          dangerFlashing = true;
          flashCycles = 0;
          flashOn = false;
          lastFlashToggle = millis();
        }
        else if (inDangerCycleMode) // — If already in danger cycle-mode, keep cycling lights
        {
          cycleLights();
        }
        else // — Otherwise show static color based on distance
        {
          cycling = false;
          if (distance <= distance_danger)
            set_traffic_light(1, 0, 0); // Danger zone → red
          else if (distance <= distance_warning)
            set_traffic_light(0, 1, 0); // Warning zone → yellow
          else
            set_traffic_light(0, 0, 1); // Safe zone → green
        }
      }
    }
  }
  else
  {
    cycleLights(); // sensor disabled → just cycle lights
  }

#ifdef PED_SIGNAL_ENABLED
  if (!testMode)
    updatePedSignal(millis());
#endif

  // Handle scheduled reboot
  if (shouldReboot && millis() >= rebootTime)
  {
    Serial.println("ESP Rebooting now...");
    ESP.restart();
  }
}