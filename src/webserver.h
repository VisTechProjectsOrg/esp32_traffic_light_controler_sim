#pragma once

#include "state.h"

// All HTTP routes, handlers and the websocket live here. Call setupWebServer()
// after SPIFFS and preferences are up.

void setupWebServer();

// serveStatic() finds a .gz sibling by itself; an explicit beginResponse() does not.
// The filesystem build stages only the compressed copy, so every hand-served page has
// to look for it and set Content-Encoding itself or the file appears to be missing.
bool spiffsPageExists(const char *path);
AsyncWebServerResponse *spiffsPage(AsyncWebServerRequest *request, const char *path);
void notifyAllClients(String message);
void notifyAllClientsDistance(float distance, int16_t &outTemp);
void listSPIFFSFiles();
