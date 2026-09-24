#pragma once

#include "state.h"

// All HTTP routes, handlers and the websocket live here. Call setupWebServer()
// after SPIFFS and preferences are up.

void setupWebServer();
void notifyAllClients(String message);
void notifyAllClientsDistance(float distance, int16_t &outTemp);
void listSPIFFSFiles();
