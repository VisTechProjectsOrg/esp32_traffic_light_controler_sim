#include "webserver.h"
#include "bench.h"
#include "signals.h"
#include "traffic.h"
#include "ped.h"
#include "proximity.h"
#include <config.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <ota_updater.h>
#include <version.h>

bool spiffsPageExists(const char *path)
{
  return SPIFFS.exists(String(path) + ".gz") || SPIFFS.exists(path);
}

AsyncWebServerResponse *spiffsPage(AsyncWebServerRequest *request, const char *path)
{
  String gz = String(path) + ".gz";
  if (SPIFFS.exists(gz))
  {
    AsyncWebServerResponse *res = request->beginResponse(SPIFFS, gz, "text/html; charset=utf-8");
    res->addHeader("Content-Encoding", "gzip");
    return res;
  }
  return request->beginResponse(SPIFFS, path, "text/html; charset=utf-8");
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

#ifdef REDIRECT_MDNS_TO_IP
  // Windows resolves a .local name by asking the router first, waiting out an answer
  // no DNS server can give, then falling back to multicast - about a second in total,
  // and unreliable enough to leave /get_config unresolved and the page half-dead.
  //
  // Bouncing the first request to the IP changes the origin, so every asset, API call
  // and the websocket afterwards need no name resolution at all. The name is resolved
  // once per navigation instead of once per request.
  //
  // macOS and iOS resolve .local natively and quickly, so they keep the friendly name.
  if (userAgent.indexOf("Windows NT") >= 0)
  {
    IPAddress addr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
    String ip = addr.toString();
    String host = request->host();
    int colon = host.indexOf(':');
    if (colon >= 0)
      host = host.substring(0, colon); // a Host header may carry the port

    // Only redirect when we know our address and are not already on it, or the
    // browser would follow the same hop forever.
    if (ip != "0.0.0.0" && host != ip)
    {
      Serial.println("Redirecting " + host + " -> " + ip + " (mDNS is slow on Windows)");
      request->redirect("http://" + ip + "/");
      return;
    }
  }
#endif

  if (spiffsPageExists("/index.html"))
  {
    cycleLights(); // TODO: leftover - serving a page should not advance the cycle
    // Never cache the shell: it carries the versioned asset URLs.
    AsyncWebServerResponse *res = spiffsPage(request, "/index.html");
    res->addHeader("Cache-Control", "no-cache");
    request->send(res);
  }
  else
  {
    Serial.println("index.html not found");
    request->send(spiffsPage(request, "/index_page_not_found.html"));
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

String identityJson()
{
  bool ap = WiFi.getMode() == WIFI_AP;
  return "{\"project\":\"esp32_traffic_light_controler_sim\"" +
         String(",\"firmware\":\"") + VERSION_FIRMWARE + "\"" +
         ",\"spiffs\":\"" + getSpiffsVersion() + "\"" +
         ",\"mac\":\"" + WiFi.macAddress() + "\"" +
         ",\"mode\":\"" + (ap ? "ap" : "station") + "\"" +
         ",\"ip\":\"" + (ap ? WiFi.softAPIP() : WiFi.localIP()).toString() + "\"}";
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
                        ",\"approach_min\":" + String(approach_min) +
                        ",\"zone_persistence\":" + String(zone_persistence) +
                        ",\"distance_sensor_enabled\":" + String(distance_sensor_enabled ? "true" : "false") +
                        ",\"distance_sensor_connected\":" + String(proximitySensorConnected() ? "true" : "false") +
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
    // Absent from older pages, and a zero here would let anything that appears take
    // over the lamps, so it is only accepted when sent and always kept in range.
    if (!doc["approach_min"].isNull())
      approach_min = constrain(doc["approach_min"].as<float>(), 0.5f, 5.0f);
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
    preferences.putFloat("appr_min", approach_min);
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

  setTestMode(doc["enabled"].as<bool>());

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

  // Outputs are held, so the GUI can light several channels at once while ringing out wires.
  if (!setTestOutput(output, state))
  {
    request->send(400, "application/json", "{\"error\": \"Unknown output\"}");
    return;
  }

  JsonDocument out;
  for (const char *name : {"red", "yellow", "green", "walk", "dont_walk", "cd_walk", "cd_dont_walk"})
    out[name] = testOutputHeld(name);
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
    ped_dw_duration = doc["dw"].as<unsigned long>() * 1000;
    ped_chained = doc["chained"].as<bool>();
    ped_fit_lights = doc["fit"].as<bool>();
    ped_chain_phase = doc["chain_phase"].as<String>() == "green" ? GREEN : RED;
    ped_walk_effective = ped_walk_duration;

    preferences.putULong("ped_walk", ped_walk_duration);
    preferences.putULong("ped_fdw", ped_fdw_duration);
    preferences.putULong("ped_dw", ped_dw_duration);
    preferences.putBool("ped_chained", ped_chained);
    preferences.putBool("ped_fit", ped_fit_lights);
    preferences.putInt("ped_chain_ph", ped_chain_phase);

    Serial.println("Ped config: walk=" + String(ped_walk_duration) +
                   "ms fdw=" + String(ped_fdw_duration) +
                   "ms chained=" + String(ped_chained));
  }

  JsonDocument out;
  out["ped_state"] = pedStateName(currentPedState);
  out["walk"] = ped_walk_duration / 1000;
  out["fdw"] = ped_fdw_duration / 1000;
  out["dw"] = ped_dw_duration / 1000;
  out["phase_total"] = pedPhaseDuration() / 1000;
  out["chained"] = ped_chained;
  out["fit"] = ped_fit_lights;
  out["min_walk"] = PED_MIN_WALK_MS / 1000;
  out["chain_phase"] = ped_chain_phase == GREEN ? "green" : "red";
  out["test_mode"] = testMode;
  String body;
  serializeJson(out, body);
  request->send(200, "application/json", body);
}
#endif

void handleProximityControl(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  JsonDocument doc;
  if (deserializeJson(doc, (const char *)data))
  {
    request->send(400, "application/json", "{\"error\": \"Invalid JSON\"}");
    return;
  }

  String action = doc["action"].as<String>();

  if (action == "relearn")
  {
    proximityRelearnBaseline();
  }

  JsonDocument out;
  out["state"] = proximityStateName(proximityState());
  out["zone"] = proximityZoneName(proximityZone());
  out["raw"] = proximityRaw();
  out["filtered"] = proximityFiltered();
  out["strength"] = proximityStrength();
  out["baseline"] = proximityBaseline();
  out["baseline_valid"] = proximityBaselineValid();
  out["connected"] = proximitySensorConnected();

  String body;
  serializeJson(out, body);
  request->send(200, "application/json", body);
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

void setupWebServer()
{
  server.on("/", HTTP_GET, handleRoot);
  server.on("/update_firmware", HTTP_GET, handleFirmwareUpdate);
  server.on("/reset_ota_state", HTTP_POST, handleFirmwareUpdateStateReset);
  server.on("/get_current_state", HTTP_GET, handleGetCurrentState);
#ifdef PED_SIGNAL_ENABLED
  server.on("/test_mode", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleTestMode);
  server.on("/set_output", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleSetOutput);
  server.on("/ped_control", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handlePedControl);
#endif
  server.on("/proximity_control", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleProximityControl);
  server.on("/get_config", HTTP_GET, handleGetConfig);
  server.on("/identify", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "application/json", identityJson()); });
  server.on("/set_config", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleFormConfig);
  server.on("/blink_mode", HTTP_GET, handlelightMode);
  server.on("/toggle_light_mode", HTTP_GET, handleToggleLightMode);
  server.on("/toggle_theme_mode", HTTP_GET, handleToggleThemeMode);

  // Assets are cached hard - they are most of the 800K and re-fetching them over
  // WiFi is what actually makes a reload slow. That is only safe because
  // tools/bump_spiffs_version.py stamps ?v=<version> onto every CSS and JS
  // reference, so a new SPIFFS build changes the URL. The HTML shell carrying
  // those URLs is sent no-cache (see handleRoot) so the change is picked up at once.
  server.serveStatic("/", SPIFFS, "/").setCacheControl("max-age=604800");
  server.serveStatic("/images", SPIFFS, "/images").setCacheControl("max-age=604800");
  server.onNotFound([](AsyncWebServerRequest *request)
                    {
    if (request->header("Accept").indexOf("application/json") != -1) {
        request->send(404, "application/json", "{\"error\":\"Not found\"}");
    } else {
        auto res = spiffsPage(request, "/index_page_not_found.html");
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
