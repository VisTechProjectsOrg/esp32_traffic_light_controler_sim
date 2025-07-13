import json

# Read versions from version.json
with open("version.json", "r") as f:
    version = json.load(f)

firmware_ver = version.get("firmware", "unknown")
spiffs_ver = version.get("spiffs", "unknown")

# Generate the README.md content
readme_content = f"""# ESP32 OTA Firmware

This branch contains OTA firmware updates for the ESP32.

**Firmware Version:** `v{firmware_ver}`  
**SPIFFS Version:** `v{spiffs_ver}`

## Files
- `firmware.bin` - main firmware binary
- `spiffs.bin` - SPIFFS filesystem image
"""

with open("README.md", "w") as f:
    f.write(readme_content)

print("README.md generated.")
