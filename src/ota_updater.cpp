// ota_updater.cpp

#include "ota_updater.h"
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include <Arduino.h>
#include <SPIFFS.h>

// Define the shared globals with external linkage
bool otaPageActive = false;
bool shouldReboot = false;
unsigned long rebootTime = 0;

// Internal state for the current upload
static bool invalidFile = false;
static bool otaError = false;
static String uploadedFilename;

/**
 * Install the OTA endpoint at POST /update
 * - onUpload: stream chunks into flash or SPIFFS
 * - onRequest: inspect flags, reply and optionally reboot
 */
void setupOTA(AsyncWebServer &server)
{
  // Ensure serial is initialized before using
  Serial.println("[OTA] Initializing OTA updater endpoint");

  server.on("/update", HTTP_POST,

            // onRequest callback (after all chunks arrive)
            [](AsyncWebServerRequest *request)
            {
      // clear any pending reboot
      shouldReboot = false;

      Serial.printf("[OTA] onRequest: file=\"%s\" invalidFile=%d otaError=%d\n", uploadedFilename.c_str(), invalidFile, otaError);

      // 0) no upload chunks received
      if (uploadedFilename.isEmpty()) {
        Serial.println("[OTA] Error: no file received");
        request->send(400, "text/plain", "INVALID_FILE");
      }
      // 1) filename not allowed
      else if (invalidFile) {
        Serial.printf("[OTA] Error: invalid filename '%s'\n", uploadedFilename.c_str());
        request->send(400, "text/plain", "INVALID_FILE");
      }
      // 2) write or end error
      else if (otaError) {
        Serial.printf("[OTA] Error: OTA failed for '%s'\n", uploadedFilename.c_str());
        request->send(500, "text/plain", "OTA_FAILED");
      }
      // 3) success: reply OK and schedule reboot
      else {
        Serial.printf("[OTA] Success: '%s' updated, scheduling reboot in 3s\n", uploadedFilename.c_str());
        request->send(200, "text/plain", "OK");
        shouldReboot = true;
        rebootTime   = millis() + 3000;
      }

      // reset for next POST
      uploadedFilename.clear();
      invalidFile = otaError = false; },

            // onUpload callback (per-chunk handler)
            [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final)
            {

      // first chunk: initialize Update
      if (index == 0) {
        invalidFile = false;
        otaError    = false;
        uploadedFilename = filename;

        size_t total = req->contentLength();
        Serial.printf("[OTA] Upload start: %s (total %u bytes)\n", filename.c_str(), total);

        if (filename == "firmware.bin") {
          Serial.println("[OTA] Beginning firmware update...");
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            otaError = true;
            Serial.println("[OTA] Error: Update.begin(firmware) failed");
            return;
          }
        }
        else if (filename == "spiffs.bin") {
          Serial.println("[OTA] Beginning SPIFFS update...");
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
            otaError = true;
            Serial.println("[OTA] Error: Update.begin(spiffs) failed");
            return;
          }
        }
        else {
          invalidFile = true;
          Serial.printf("[OTA] Error: filename '%s' not recognized\n", filename.c_str());
          return;
        }
      }

      // write data if still valid
      if (!invalidFile && !otaError) {
        // Serial.printf("[OTA] Writing chunk %u, size %u\n", (unsigned)index, (unsigned)len);
        size_t written = Update.write(data, len);
        if (written != len) {
          otaError = true;
          Serial.printf("[OTA] Error: wrote %u of %u bytes\n", (unsigned)written, (unsigned)len);
        }
      }

      // finalize when done
      if (final) {
        Serial.println("[OTA] Finalizing update...");
        if (!invalidFile && !otaError) {
          if (Update.end(true)) {
            Serial.println("[OTA] Update.end(true) succeeded");
          } else {
            otaError = true;
            Serial.println("[OTA] Error: Update.end(true) failed");
          }
        }
      } });
}

/**
 * Serve the firmware update page at GET /firmware_update
 */
void handleFirmwareUpdate(AsyncWebServerRequest *request)
{
  Serial.println("[OTA] Serving firmware update page");
  otaPageActive = true;
  if (SPIFFS.exists("/index_firmware_update.html"))
  {
    request->send(SPIFFS, "/index_firmware_update.html", "text/html; charset=utf-8");
  }
  else
  {
    Serial.println("[OTA] Error: OTA page not found");
    request->send(SPIFFS, "/index_page_not_found.html", "text/html; charset=utf-8");
  }
}

/**
 * Call in loop() to perform the deferred reboot when scheduled
 */
void handleReboot()
{
  if (shouldReboot && millis() >= rebootTime)
  {
    Serial.println("[OTA] Rebooting now...");
    ESP.restart();
  }
}
