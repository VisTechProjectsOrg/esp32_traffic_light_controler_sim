// WifiManager.cpp
#include "WifiManager.h"
#include <ESPmDNS.h>

using namespace WifiManager;

static AsyncWebServer* _serverPtr = nullptr;
static String          _mdnsName;

// Called when station gets an IP
static void onGotIP(arduino_event_id_t event, arduino_event_info_t info) {
    Serial.printf("✅ STA connected, IP %s\n",
                  WiFi.localIP().toString().c_str());
    if (MDNS.begin(_mdnsName.c_str())) {
        Serial.printf("🔍 mDNS started as %s.local\n",
                      _mdnsName.c_str());
    }
    if (_serverPtr) {
        _serverPtr->begin();
    }
}

// Called when station disconnects
static void onLostIP(arduino_event_id_t event, arduino_event_info_t info) {
    Serial.println("⚠️ STA lost—reconnecting");
    WiFi.reconnect();
}

// Called when AP starts
static void onAPStart(arduino_event_id_t event, arduino_event_info_t info) {
    Serial.printf("✅ AP started, IP %s\n",
                  WiFi.softAPIP().toString().c_str());
    if (_serverPtr) {
        _serverPtr->begin();
    }
}

void WifiManager::beginStation(const char* ssid, const char* pass,
                               AsyncWebServer &server,
                               const char* mdnsName) {
    _serverPtr = &server;
    _mdnsName  = mdnsName;

    WiFi.onEvent(onGotIP,  ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(onLostIP, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    WiFi.begin(ssid, pass);
    Serial.printf("Connecting to STA %s…\n", ssid);
}

void WifiManager::beginAP(const char* ssid, const char* pass,
                          AsyncWebServer &server) {
    _serverPtr = &server;

    WiFi.onEvent(onAPStart, ARDUINO_EVENT_WIFI_AP_START);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, pass);
    Serial.printf("Starting AP %s…\n", ssid);
}
