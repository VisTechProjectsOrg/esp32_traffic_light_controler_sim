// WifiManager.h
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

namespace WifiManager {
    /**
     * @brief Initialize ESP32 as Wi‑Fi station (STA) mode, non‑blocking.
     * @param ssid     SSID to connect
     * @param pass     WPA‑PSK
     * @param server   AsyncWebServer instance (will be started on IP-up event)
     * @param mdnsName Optional mDNS name (e.g. "trafficlights")
     */
    void beginStation(const char* ssid, const char* pass,
                      AsyncWebServer &server,
                      const char* mdnsName = "trafficlights");

    /**
     * @brief Initialize ESP32 as Wi‑Fi Access Point (AP) mode, non‑blocking.
     * @param ssid   SSID to host
     * @param pass   WPA‑PSK (empty = open)
     * @param server AsyncWebServer instance (will be started on AP-start event)
     */
    void beginAP(const char* ssid, const char* pass,
                 AsyncWebServer &server);
}

#endif // WIFI_MANAGER_H