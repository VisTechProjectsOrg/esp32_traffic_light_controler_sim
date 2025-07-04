#pragma once
#include <ESPAsyncWebServer.h>

void setupOTA(AsyncWebServer& server);
void handleFirmwareUpdate(AsyncWebServerRequest *request);
