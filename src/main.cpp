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

WebSocketsServer webSocket = WebSocketsServer(81); // Port 81 for WebSocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;

// ANY VARIBLE CHANGES, MODIFY THE CONFIG.H FILE
// *** Remeber to build and upload all SPIFFS files!!! ****

enum LightState
{
  RED,
  GREEN,
  YELLOW,
  OFF
};

struct DelaySetting
{
  const char *key;
  uint32_t defaultValue;
};

LightState currentLightState = OFF;
LightState previousLightState = OFF;
unsigned long previousMillis = 0;
unsigned long currentDelay = 0;
unsigned long LED_red_delay = 0;
unsigned long LED_yellow_delay = 0;
unsigned long LED_green_delay = 0;

bool lightMode = false;
bool themeMode = false;
bool blinkState = false;
bool randomBlinkMode = false;
bool blinkAllColors = true; // Default to blinking all colors
int blinkPin = -1;
unsigned long lastBlinkMillis = 0;

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
    Serial.println("Cat Mode: " + state);
    String jsonResponse = "{\"state\":\"" + state + "_cat\"}";
    ws.textAll(jsonResponse);
  }
  else
  {
    Serial.println("Normal Mode: " + state);
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
      currentDelay = LED_green_delay;
      break;
    case GREEN: // if green turn it to yellow
      currentLightState = YELLOW;
      currentDelay = LED_yellow_delay;
      break;
    case YELLOW: // if yellow turn it to red
      currentLightState = RED;
      currentDelay = LED_red_delay;
      break;
    case OFF: // if off turn it to red?
      currentLightState = RED;
      currentDelay = LED_red_delay;
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
    request->send(SPIFFS, "/index.html", "text/html");
  }
  else
  {
    Serial.println("index.html not found");
    request->send(404, "/index_page_not_found.html", "text/html");
  }
}

void handleGetDelays(AsyncWebServerRequest *request)
{
  Serial.println("Sending get delays");

  String jsonResponse = "{\"red_delay\":" + String(LED_red_delay) +
                        ",\"yellow_delay\":" + String(LED_yellow_delay) +
                        ",\"green_delay\":" + String(LED_green_delay) + "}";

  request->send(200, "application/json", jsonResponse);
}

void handleFormSetDelay(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
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
  float red = doc["red_delay"];
  float yellow = doc["yellow_delay"];
  float green = doc["green_delay"];

  Serial.println("Action: " + action);
  Serial.println("red: " + String(red));
  Serial.println("yellow: " + String(yellow));
  Serial.println("green: " + String(green));

  if (action == "set_delays")
  {
    preferences.putULong("red_delay", red * 1000);
    preferences.putULong("yellow_delay", yellow * 1000);
    preferences.putULong("green_delay", green * 1000);

    LED_red_delay = preferences.getULong("red_delay", 5000);
    LED_yellow_delay = preferences.getULong("yellow_delay", 3000);
    LED_green_delay = preferences.getULong("green_delay", 6000);

    Serial.println("Updated Delays - R: " + String(LED_red_delay) + "ms Y: " + String(LED_yellow_delay) + "ms G: " + String(LED_green_delay) + "ms");
    request->send(200, "application/json", "{\"message\": \"Delays updated!\"}");
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

  if (use_wifi)
  {
    // WIFI
    WiFi.begin(ssid, password);
    Serial.println("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    // AP
    WiFi.softAP(AP_ssid); // Set up the AP without a password
    Serial.println("Access Point Started");
    Serial.print("IP address: ");
    Serial.println(WiFi.softAPIP());
  }

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

  DelaySetting delays[] = {
      {"red_delay", 5000},
      {"yellow_delay", 3000},
      {"green_delay", 6000}};

  for (const auto &delay : delays)
  {
    if (!preferences.isKey(delay.key))
    {
      preferences.putULong(delay.key, delay.defaultValue);
    }
  }

  LED_red_delay = preferences.getULong("red_delay", 5000);
  LED_yellow_delay = preferences.getULong("yellow_delay", 3000);
  LED_green_delay = preferences.getULong("green_delay", 6000);

  // listSPIFFSFiles();

  webSocket.begin(); // Start WebSocket server

  server.on("/", HTTP_GET, handleRoot);
  // server.on("/set_delay", HTTP_GET, handleSetDelay);
  server.on("/get_delays", HTTP_GET, handleGetDelays);
  server.on("/get_current_state", HTTP_GET, handleGetCurrentState);
  server.on("/blink_mode", HTTP_GET, handlelightMode);
  server.on("/toggle_light_mode", HTTP_GET, handleToggleLightMode);
  server.on("/toggle_theme_mode", HTTP_GET, handleToggleThemeMode);
  server.on("/set_delays", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleFormSetDelay);

  //   server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
  //     request->send(200, "application/javascript", "script.js", [](AsyncWebServerResponse *response){
  //         response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  //     });
  // });

  server.serveStatic("/", SPIFFS, "/");
  server.serveStatic("/images", SPIFFS, "/images");
  server.onNotFound([](AsyncWebServerRequest *request)
                    { request->send(302, "text/plain", "What ever you where looking for, was not found..."); });

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
}

void loop()
{
  cycleLights();
}