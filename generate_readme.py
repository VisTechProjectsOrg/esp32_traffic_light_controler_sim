import re

with open("src/version.h", "r") as f:
    config = f.read()

# Match version strings
fw_match = re.search(r'#define VERSION_FIRMWARE\s+"(.+?)"', config)
spiffs_match = re.search(r'#define VERSION_SPIFFS\s+"(.+?)"', config)

# Raise errors if any version is missing
if not fw_match:
    raise ValueError("Firmware version (VERSION_FIRMWARE) not found in src/version.h")
if not spiffs_match:
    raise ValueError("SPIFFS version (VERSION_SPIFFS) not found in src/version.h")

# Fallbacks if not found
firmware_version = fw_match.group(1) if fw_match else "unknown"
spiffs_version = spiffs_match.group(1) if spiffs_match else "unknown"

# Load template
with open("readme_template.md", "r") as f:
    template = f.read()

# Replace placeholders
readme = (
    template.replace("{{FIRMWARE_VERSION}}", firmware_version)
            .replace("{{SPIFFS_VERSION}}", spiffs_version)
)

# Write final README
with open("README.md", "w") as f:
    f.write(readme)

print("README.md updated from template.")
