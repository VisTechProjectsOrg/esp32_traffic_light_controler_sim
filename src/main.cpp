#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <config.h>

#include "state.h"
#include "signals.h"
#include "traffic.h"
#include "ped.h"
#include "proximity.h"
#include "webserver.h"
#include "WiFiManager.h"
#include <ota_updater.h>

// **** ANY VARIBLE CHANGES, MODIFY THE CONFIG.H FILE ****
// **** Remeber to build and upload all SPIFFS files!!! ****

void setup()
{
  Serial.begin(115200);
  Serial.println("Starting");

  setupSignalPins();
  setupProximity();


// 1) Prevent ambiguous dual‑modes:
#if defined(WIFI_SSID) && defined(WIFI_PASS) && defined(AP_SSID)
#error "You cannot define both WIFI_SSID/PASS and AP_SSID (with or without AP_PASS)."
#endif

// 2) Station mode if both creds are set
#if defined(WIFI_SSID) && defined(WIFI_PASS)
  WifiManager::beginStation(WIFI_SSID, WIFI_PASS, server);
// 3) AP mode if SSID is set
#elif defined(AP_SSID)
// 3a) Secured AP if password provided
#ifdef AP_PASS
  WifiManager::beginAP(AP_SSID, AP_PASS, server);
// 3b) Open AP otherwise
#else
  WifiManager::beginAP(AP_SSID, /* open‑auth */ "", server);
#endif
// 4) Error if neither mode was configured
#else
#error "You must define either WIFI_SSID/PASS or AP_SSID (optionally AP_PASS) in config.h"
#endif

  if (!MDNS.begin("trafficlights"))
  {
    Serial.println("Error setting up MDNS responder!");
  }

  if (!SPIFFS.begin(false))
  {
    Serial.println("Failed to mount file system");
    return;
  }

  preferences.begin("traffic-light", false);

  // Set defaults for timing delays
  if (!preferences.isKey("delay_red"))
    preferences.putULong("delay_red", 5000);
  if (!preferences.isKey("delay_yellow"))
    preferences.putULong("delay_yellow", 3000);
  if (!preferences.isKey("delay_green"))
    preferences.putULong("delay_green", 6000);

  // Set defaults for distance zones (stored as float)
  if (!preferences.isKey("dist_max"))
    preferences.putFloat("dist_max", 15.0);
  if (!preferences.isKey("dist_warn"))
    preferences.putFloat("dist_warn", 5.0);
  if (!preferences.isKey("dist_dang"))
    preferences.putFloat("dist_dang", 2.0);
  if (!preferences.isKey("zone_persist"))
    preferences.putInt("zone_persist", 3);

#ifdef PED_SIGNAL_ENABLED
  if (!preferences.isKey("ped_walk"))
    preferences.putULong("ped_walk", 7000);
  if (!preferences.isKey("ped_fdw"))
    preferences.putULong("ped_fdw", 15000);
  if (!preferences.isKey("ped_dw"))
    preferences.putULong("ped_dw", 3000);
  if (!preferences.isKey("ped_chained"))
    preferences.putBool("ped_chained", true);
  if (!preferences.isKey("ped_chain_ph"))
    preferences.putInt("ped_chain_ph", RED);
#endif

  // Load timing delays
  LED_delay_red = preferences.getULong("delay_red", 5000);
  LED_delay_yellow = preferences.getULong("delay_yellow", 3000);
  LED_delay_green = preferences.getULong("delay_green", 6000);

// Distance sensor: if programaticly enabled, load status from preferences
#ifdef DISTANCE_SENSOR_ENABLED
  distance_sensor_enabled = preferences.getBool("dist_sens_en", false);
#else
  distance_sensor_enabled = false;
#endif

  Serial.println("Distance sensor enabled: " + String(distance_sensor_enabled ? "true" : "false"));

  distance_max = preferences.getFloat("dist_max", 15.0);
  distance_warning = preferences.getFloat("dist_warn", 5.0);
  distance_danger = preferences.getFloat("dist_dang", 2.0);
  zone_persistence = preferences.getInt("zone_persist", 3);

#ifdef PED_SIGNAL_ENABLED
  ped_walk_duration = preferences.getULong("ped_walk", 7000);
  ped_walk_effective = ped_walk_duration;
  ped_fdw_duration = preferences.getULong("ped_fdw", 15000);
  ped_dw_duration = preferences.getULong("ped_dw", 3000);
  ped_chained = preferences.getBool("ped_chained", true);
  ped_chain_phase = (LightState)preferences.getInt("ped_chain_ph", RED);
#endif


  setupWebServer();

#ifdef PED_SIGNAL_ENABLED
  setPedState(PED_DONT_WALK);
#endif
}

void loop()
{
  unsigned long now = millis();

  ws.cleanupClients();

  if (testMode)
    return; // the bench GUI owns the outputs

  // Proximity can borrow the vehicle lamps, but nothing stops a clock. The traffic
  // phase machine and the pedestrian phase both advance every pass regardless; all
  // proximity decides is who writes the vehicle lamps right now. When it hands them
  // back the cycle repaints at whatever phase it has reached in the meantime.
  bool proximityOwnsLamps = false;
  if (!otaPageActive)
    proximityOwnsLamps = proximityUpdate(now);

  setTrafficOutputOwner(proximityOwnsLamps);
  cycleLights();

#ifdef PED_SIGNAL_ENABLED
  updatePedSignal(now);
#endif

#ifdef AP_SSID
  WifiManager::captivePortalLoop(); // no-op unless the captive DNS actually started
#endif

  if (shouldReboot && millis() >= rebootTime)
  {
    Serial.println("ESP Rebooting now...");
    ESP.restart();
  }
}
