#include "ota_updater.h"
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include <Arduino.h>
#include <SPIFFS.h>

void setupOTA(AsyncWebServer &server)
{
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    bool hasError = Update.hasError();
 request->send(200, "text/plain", "OK");

static bool shouldReboot = false;

if (!Update.hasError()) {
  Serial.println("✅ OTA Update complete. Will reboot in 3 seconds...");
  shouldReboot = true;
  // Delay reboot in the main loop
  xTaskCreate(
    [](void *) {
      delay(3000);
      ESP.restart();
    },
    "delayedReboot", 4096, NULL, 1, NULL
  );
} }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
            {
    if (!index) {
      Serial.printf("OTA Start: %s\n", filename.c_str());
      if (filename == "firmware.bin") {
        Serial.println("Starting OTA for firmware update");
        Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
      } else if (filename == "spiffs.bin") {
        Serial.println("Starting OTA for SPIFFS update");
        Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS);
      } else {
        Serial.println("Invalid file type for OTA update");
        request->send(400, "text/plain", "Invalid file type");
        return;
      }
    }

    if (!Update.hasError()) {
      if (Update.write(data, len) != len) {
        Serial.println("OTA Write failed.");
      }
    }

    if (final) {
      if (Update.end(true)) {
        Serial.printf("OTA Success: %u bytes\n", index + len);
      } else {
        Serial.printf("OTA Error: %s\n", Update.errorString());
      }
    } });
}

void handleFirmwareUpdate(AsyncWebServerRequest *request)
{
  IPAddress requesterIP = request->client()->remoteIP();

  // Get the User-Agent (if present)
  String userAgent = request->header("User-Agent");

  Serial.print("Got firmware request from IP: ");
  Serial.println(requesterIP);

  Serial.print("User-Agent: ");
  Serial.println(userAgent);

  if (SPIFFS.exists("/index_firmware_update.html"))
  {
    request->send(SPIFFS, "/index_firmware_update.html", "text/html; charset=utf-8");
  }
  else
  {
    Serial.println("index_firmware_update.html not found");
    request->send(404, "/index_page_not_found.html", "text/html");
  }
}
