import re

# Path to config.h
config_path = "src/config.h"

with open(config_path, "r") as f:
    content = f.read()

# Extract versions using regex
fw_match = re.search(r'#define VERSION_FIRMWARE\s+"(.+?)"', content)
spiffs_match = re.search(r'#define VERSION_SPIFFS\s+"(.+?)"', content)

firmware_ver = fw_match.group(1) if fw_match else "unknown"
spiffs_ver = spiffs_match.group(1) if spiffs_match else "unknown"

# Create README content
readme = f"""
# ESP32 Traffic Light Controller Simulator TEST 123

**Firmware Version:** {firmware_ver}  
**SPIFFS Version:** {spiffs_ver}

This project simulates a traffic light controller using ESP32.

## Features
- WiFi connectivity
- Configurable delays
- Distance-based flashing logic

"""

with open("README_test.md", "w") as f:
    f.write(readme)

print("README.md updated.")
