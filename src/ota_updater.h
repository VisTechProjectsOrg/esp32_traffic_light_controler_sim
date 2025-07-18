#pragma once
#include <ESPAsyncWebServer.h>

extern bool otaPageActive;
extern bool shouldReboot;

extern unsigned long rebootTime;

void setupOTA(AsyncWebServer &server);
void handleFirmwareUpdate(AsyncWebServerRequest *request);
