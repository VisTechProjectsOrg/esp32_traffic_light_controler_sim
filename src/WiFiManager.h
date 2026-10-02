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
    /**
     * @brief Pump the captive DNS. Call every loop in AP mode; a no-op otherwise.
     *
     * In AP mode the device is also the DHCP server, so it hands out itself as the
     * DNS server and can answer every query with its own address. That is what lets
     * any hostname reach the device without anyone knowing its IP, which matters
     * where there is no router to put a reservation on.
     */
    void captivePortalLoop();

    void beginAP(const char* ssid, const char* pass,
                 AsyncWebServer &server);
}

#endif // WIFI_MANAGER_H