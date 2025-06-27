#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <config.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <TFMPlus.h>

WebSocketsServer webSocket = WebSocketsServer(81); // Port 81 for WebSocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;
TFMPlus tfm;
HardwareSerial tfmSerial(2); // Use UART2: GPIO16 = RX, GPIO17 = TX (default)

// **** ANY VARIBLE CHANGES, MODIFY THE CONFIG.H FILE ****
// **** Remeber to build and upload all SPIFFS files!!! ****

enum LightState
{
  RED,
  GREEN,
  YELLOW,
  OFF
};

struct DefaultSetting
{
  const char *key;
  uint32_t defaultValue;
};

LightState currentLightState = OFF;
LightState previousLightState = OFF;

unsigned long previousMillis = 0;
unsigned long currentDelay = 0;

unsigned long LED_delay_red = 0;
unsigned long LED_delay_yellow = 0;
unsigned long LED_delay_green = 0;

// default delays for the traffic light, get overwritten by the web interface
unsigned long distance_max = 0;
unsigned long distance_warning = 0;
unsigned long distance_danger = 0;

bool distance_sensor_enabled = false; // Default to false

bool lightMode = false;
bool themeMode = false;
bool blinkState = false;
bool blinkAllColors = true; // Default to blinking all colors drop down menu
bool randomBlinkMode = false;

int blinkPin = -1;

unsigned long lastBlinkMillis = 0;

int getDistance()
{
  int16_t distance, strength, temperature;

  if (tfm.getData(distance, strength, temperature))
  {
    return distance; // Return the distance in cm
  }
  else
  {
    Serial.println("Failed to read from TFMini-Plus.");
    return -1; // Return -1 if reading fails
  }
}

void set_traffic_light(boolean LED_red_state, boolean LED_yellow_state, boolean LED_green_state)
{
  // Serial.println("Changing traffic light color: "
  //  "Red: " +
  //  String(LED_red_state ? "ON" : "OFF") +
  //  " Yellow: " + String(LED_yellow_state ? "ON" : "OFF") +
  //  " Green: " + String(LED_green_state ? "ON" : "OFF"));

  // output and invert the logic here for relays
  digitalWrite(LED_red_pin, !LED_red_state);
  digitalWrite(LED_yellow_pin, !LED_yellow_state);
  digitalWrite(LED_green_pin, !LED_green_state);

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
      }
      else
      {
        if (randomBlinkMode)
        {
          // Serial.println("Random Blink Mode function");
          // Randomly select a pin to blink
          int randomColor = random(3); // Generates a random number from 0 to 2 (3 colors)

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

          blinkAllColors = false; // Disable blinking all colors, since we're doing a single color
          blinkState = true;      // Reset blink state

          digitalWrite(blinkPin, !blinkState); // Invert the output state
        }
        else
        {
          digitalWrite(blinkPin, !blinkState); // Invert the output state
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
  }
}

void notifyAllClients(String message)
{
  for (int i = 0; i < ws.count(); i++)
  {
    ws.text(i, message);
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  { // ment to send popup message telling the user to refresh the page on reconnect
  case WStype_DISCONNECTED:
    Serial.printf("Client %u disconnected\n", num);
    break;
  case WStype_CONNECTED:
    Serial.printf("Client %u connected from %s\n", num, webSocket.remoteIP(num).toString().c_str());
    // Send a message to the client once connected (optional)
    // webSocket.sendTXT(num, "Hello from ESP32");
    break;
  case WStype_TEXT:
    Serial.printf("Message from client %u: %s\n", num, payload);
    break;
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

  if (SPIFFS.exists("/index.html"))
  {
    cycleLights();
    request->send(SPIFFS, "/index.html", "text/html; charset=utf-8");
  }
  else
  {
    Serial.println("index.html not found");
    request->send(404, "/index_page_not_found.html", "text/html");
  }
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
                        ",\"distance_sensor_enabled\":" + String(distance_sensor_enabled ? "true" : "false") +
                        ",\"version\":\"" + String(VERSION) + "\"}";

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
    distance_max = doc["distance_max"].as<unsigned long>();
    distance_warning = doc["distance_warning"].as<unsigned long>();
    distance_danger = doc["distance_danger"].as<unsigned long>();
    distance_sensor_enabled = doc["distance_sensor_enabled"].as<bool>();

    Serial.println("Action: " + action);
    Serial.println("delay_red: " + String(LED_delay_red));
    Serial.println("delay_yellow: " + String(LED_delay_yellow));
    Serial.println("delay_green: " + String(LED_delay_green));
    Serial.println("distance_max: " + String(distance_max));
    Serial.println("distance_warning: " + String(distance_warning));
    Serial.println("distance_danger: " + String(distance_danger));
    Serial.println("distance_sensor_enabled: " + String(distance_sensor_enabled));

    // Convert float to unsigned long for storage in Preferences

    // Save to Preferences
    preferences.putULong("delay_red", LED_delay_red);
    preferences.putULong("delay_yellow", LED_delay_yellow);
    preferences.putULong("delay_green", LED_delay_green);
    preferences.putULong("dist_max", distance_max);
    preferences.putULong("dist_warn", distance_warning);
    preferences.putULong("dist_dang", distance_danger);
    preferences.putBool("dist_sens_en", distance_sensor_enabled);

    // StaticJsonDocument<200> responseDoc;
    JsonDocument responseDoc;
    responseDoc["message"] = "Config updated!";
    responseDoc["delay_red"] = preferences.getULong("delay_red", -1);
    responseDoc["delay_yellow"] = preferences.getULong("delay_yellow", -1);
    responseDoc["delay_green"] = preferences.getULong("delay_green", -1);
    responseDoc["distance_max"] = preferences.getULong("dist_max", -1);
    responseDoc["distance_warning"] = preferences.getULong("dist_warn", -1);
    responseDoc["distance_danger"] = preferences.getULong("dist_dang", -1);
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
    digitalWrite(LED_red_pin, LOW); // Turn off the blinking LEDs
    digitalWrite(LED_yellow_pin, LOW);
    digitalWrite(LED_green_pin, LOW);
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

void notifyAllClientsDistance(int distance)
{
  String jsonResponse;
  if (distance == -1)
  {
    jsonResponse = "{\"distance_cm\":null}";
  }
  else
  {
    jsonResponse = "{\"distance_cm\":" + String(distance) + "}";
  }
  ws.textAll(jsonResponse);
  // Serial.println("Distance: " + String(distance) + " cm | notify all clients");
}

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
  Serial.println("Starting");

  pinMode(LED_red_pin, OUTPUT);
  pinMode(LED_yellow_pin, OUTPUT);
  pinMode(LED_green_pin, OUTPUT);

  set_traffic_light(0, 0, 0);

  tfmSerial.begin(115200, SERIAL_8N1, 16, 17); // Start TFMini Serial
  delay(100);

  if (tfm.begin(&tfmSerial))
  {
    // Optional: run self-check
    Serial.println("Running TFMini self-check...");
    tfm.sendCommand(0x01, 0); // 0x01 is the self-check command
    delay(500);               // Give it some time
    Serial.println("Self-check complete (no result returned).");
  }
  else
  {
    Serial.println("Failed to connect to TFMini-Plus.");
  }

#ifdef WIFI_SSID
  // WIFI
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
#else
// AP
#ifdef AP_PASS
  WiFi.softAP(AP_SSID, AP_PASS);
#else
  WiFi.softAP(AP_SSID);
  Serial.println("No AP password defined, setting up AP without password");
#endif
  Serial.println("Access Point Started");
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());
#endif

  if (!MDNS.begin("trafficlights"))
  {
    Serial.println("Error setting up MDNS responder!");
  }

  if (!SPIFFS.begin(true))
  {
    Serial.println("Failed to mount file system");
    return;
  }

  preferences.begin("traffic-light", false);

  DefaultSetting defaultSettings[] = {
      {"delay_red", 5000},
      {"delay_yellow", 3000},
      {"delay_green", 6000},
      {"dist_max", 150},
      {"dist_warn", 40},
      {"dist_dang", 10}};

  for (const auto &setting : defaultSettings)
  {
    if (!preferences.isKey(setting.key))
    {
      preferences.putULong(setting.key, setting.defaultValue);
    }
  }

  // Load timing delays
  LED_delay_red = preferences.getULong("delay_red", -1);
  LED_delay_yellow = preferences.getULong("delay_yellow", -1);
  LED_delay_green = preferences.getULong("delay_green", -1);

// Distance sensor: if programaticly enabled, load status from preferences
#ifdef DISTANCE_SENSOR_ENABLED
  distance_sensor_enabled = preferences.getBool("dist_sens_en", false);
#else
  distance_sensor_enabled = false; // Default to false if not defined
#endif

  Serial.println("Distance sensor enabled: " + String(distance_sensor_enabled ? "true" : "false"));

  distance_max = preferences.getULong("dist_max", -1);
  distance_warning = preferences.getULong("dist_warn", -1);
  distance_danger = preferences.getULong("dist_dang", -1);

  // listSPIFFSFiles();

  webSocket.begin(); // Start WebSocket server

  server.on("/", HTTP_GET, handleRoot);
  server.on("/get_current_state", HTTP_GET, handleGetCurrentState);
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
        request->send(404, "text/plain", "What ever you were looking for, was not found...");
    } });

  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
             {
              if (type == WS_EVT_CONNECT) {
                Serial.println("WebSocket client connected");
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
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("Hostname: ");
  Serial.println(WiFi.getHostname());
}

void loop()
{
  // State variables for distance sensing & danger flashing
  static unsigned long lastDistanceCheck = 0;
  static unsigned long dangerStartTime = 0;
  static bool cycling = true;
  static bool hasFlashedInDanger = false;
  static bool inDangerCycleMode = false;

  // ⬇ New non-blocking flash state
  static bool dangerFlashing = false;
  static int flashCycles = 0;
  static bool flashOn = false;
  static unsigned long lastFlashToggle = 0;

  int distance = -1;

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
    return; // skip the rest of loop() while flashing
  }

  // —— 2) Distance-sensor logic ——
  if (distance_sensor_enabled)
  {
    // only check twice per second
    if (millis() - lastDistanceCheck >= 500)
    {
      lastDistanceCheck = millis();
      distance = getDistance();
      notifyAllClientsDistance(distance);

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
        return; // hand off to flashing block above
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
  else
  {
    cycleLights(); // sensor disabled → just cycle lights
  }
}