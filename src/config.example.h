#pragma once

// Copy this file to config.h and edit that. config.h is ignored by git because it
// holds the WiFi credentials; this copy is what a fresh clone and the release build use.

// Pins for the relay boards. All non-strapping plain outputs, assigned in channel
// order so the ribbon to each board runs straight. 16 is the onboard RGB and 25/26
// are the TF-Luna UART, so those are kept clear.
//
//   board 1          board 2
//   ch1 red    21     ch5 ped DON'T WALK   32
//   ch2 yellow 22     ch6 ped WALK         33
//   ch3 green  23     ch7 countdown D/W    18
//   ch4 spare         ch8 countdown WALK   19
#define LED_red_pin 21
#define LED_yellow_pin 22
#define LED_green_pin 23

static unsigned long blinkInterval = 1000;        // Traffic light blink mode interval in milliseconds
const unsigned long dangerHoldTime = 1000; // How long you must stay in the danger zone before flashing (1s)

// UART for TF-Luna
#define DISTANCE_SENSOR_ENABLED // programticly enable distance sensor

// Onboard RGB LED (Freenove ESP32-WROOM)
#define RGB_LED_ENABLED
#define RGB_LED_PIN 16
// 0-255. It is a status pilot sitting next to you on the desk, not a lamp - at full
// scale it is uncomfortable to look at. 64 is about a quarter.
#define RGB_LED_BRIGHTNESS 64

// for connecting to wifi, uncomment both
// #define WIFI_SSID "your-network"
// #define WIFI_PASS "your-password"

// or set up an access point
#define AP_SSID "Traffic Lights"
// #define AP_PASS "12345678" //uncomment to add a password, 8 char minimum
#define PED_SIGNAL_ENABLED
// pedestrian signal, second relay board. Both modules are 3-wire per ITE PTCSI:
// orange (hand), blue (walking person), white (common). Two switched hots each.
#define PED_dont_walk_pin 32 // orange - DON'T WALK / hand, also flashes for FDW
#define PED_walk_pin 33      // blue   - WALK / man

// The countdown gets its own pair rather than being spliced in parallel with the
// combo head, because proximity mode flashes that hand symbol for an unrelated
// warning and a parallel countdown would read it as a clearance interval and
// relearn a bogus value. Separate channels also allow timing it independently.
#define PED_countdown_dont_walk_pin 18
#define PED_countdown_walk_pin 19

// Send Windows clients that arrive by mDNS name to the IP instead. Resolving a
// .local name there costs about a second and fails often; after the redirect the
// origin is the IP and nothing resolves a name again. Comment out to always keep
// the friendly name in the address bar.
#define REDIRECT_MDNS_TO_IP

// FDW flash half-period. MUTCD is 1 Hz at 50% duty, and the countdown module
// detects the flashing to know it should be counting - do not change this.
const unsigned long pedFdwFlashInterval = 500;
