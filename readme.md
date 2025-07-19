# Traffic Light Signal Controller 🚦<code>v0.1</code>

### Current versions:  [![Firmware](https://img.shields.io/badge/Firmware-v0.1-blue)](https://github.com/VisTechProjects/esp32_traffic_light_controler_sim/tree/firmware_prod) [![v0.1](https://img.shields.io/badge/SPIFFS-v0.1-orange)](https://github.com/VisTechProjects/esp32_traffic_light_controler_sim/tree/firmware_prod)

<div style="display: flex; align-items: center; flex-wrap: nowrap;">
    <span style="font-size: 20px; max-width: 500px; text-align: justify;">
        This random project is a traffic light controller. The controller mimics the behavior of a standard 
        traffic light system, cycling through red, yellow, and green lights. Use it with LEDs or connect some 
        relays to a real traffic light!
    </span>
</div><br>
<div>
    <img src="data/img/traffic_lt/all_on.png" alt="Traffic Light" width="100" height="200" style="margin-right: 10px;">
    <img src="images/traffic_light.gif" alt="Traffic Light" width="100" height="200" style="margin-right: 10px;">
</div>

## Features

- **Realistic timing**: The controller uses realistic timing intervals for each light phase
- **Self hosted WifI AP**: Create it's own WiFi AP or connect to your Wifi
- **Multi client synchronized handling**: Synchronized page events
- **Configurable timing**: You can easily configure the timing duration of each light phase
- ***Cat mode***: Must needed mode, Self explanatory as pictured above
- **State management**: Efficient state management to handle the transitions between different light phases and clients
- **PlatformIO integration**: Built using PlatformIO for easy project management and deployment


## Why did I make this...

Because why tf not, I had nothing better to do and I acquired a broken traffic... and **of course** it needed a awesome wifi controller, and it was fun to build.

## Configuration (ESP32 or other wifi module)

Use [VSCode](https://code.visualstudio.com/) with [PlatformIO](https://platformio.org/install/ide?install=vscode) to compile the and upload the code to your ESP.

You can configure the pinouts light by modifying the `config.h` file:
```cpp
#define LED_red_pin 12
#define LED_yellow_pin 14
#define LED_green_pin 27
```

Configure delays and distance sensor

```cpp
unsigned long blinkInterval = 1000;        // Traffic light blink mode interval in milliseconds
const unsigned long dangerHoldTime = 3000; // How long you must stay in the danger zone before flashing (3s)

#define DISTANCE_SENSOR_ENABLED // programticly enable distance sensor
```

As well as wifi/AP info
```cpp
// for connecting to a network, use_wifi must be true
const char *ssid = "SSID";
const char *password = "PW";

// or set up an open (or protected) access point, uncommenting will override connecting to wifi
const char *AP_ssid = "Traffic Lights";
// const char *AP_pass = "1234578"; //uncomment me to add a password
```

## Site configuration

To configure the light cycle delays, distance warnings or firmware updates use the **Settings menu**:

<div style="display: flex; align-items: center;">
    <img src="images/options_menu.png" alt="Traffic Light" width="400" height="430">
</div>

## Site preview

<div style="display: flex; align-items: center;">
    <img src="images/main_page.png" alt="Traffic Light" width="572" height="501">
</div>

<br>

## OTA updates:

OTA updates are available (if connected to wifi)

<div style="display: flex; align-items: center;">
    <table>
      <tr>
        <td><img src="images/ota_page.png" alt="OTA Updates" width="500" height="431"></td>
        <td><img src="images/ota_page_update_avail.png" alt="OTA Updates" width="500" height="431"></td>
      </tr>
    </table>
</div>

<br>


## Current firmware version available:

[![Firmware](https://img.shields.io/badge/Firmware-v0.1-blue)](https://github.com/VisTechProjects/esp32_traffic_light_controler_sim/tree/firmware_prod) <br> [![v0.1](https://img.shields.io/badge/SPIFFS-v0.1-orange)](https://github.com/VisTechProjects/esp32_traffic_light_controler_sim/tree/firmware_prod)

### [Firmware Repo source](https://github.com/VisTechProjects/esp32_traffic_light_controler_sim/tree/firmware_prod)