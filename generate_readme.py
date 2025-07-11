import re

with open("src/config.h", "r") as f:
    config = f.read()

# Match version strings
fw_match = re.search(r'#define VERSION_FIRMWARE\s+"(.+?)"', config)
spiffs_match = re.search(r'#define VERSION_SPIFFS\s+"(.+?)"', config)

# Fallbacks if not found
firmware_version = fw_match.group(1) if fw_match else "unknown"
spiffs_version = spiffs_match.group(1) if spiffs_match else "unknown"

# Load template
with open("README_template.md", "r") as f:
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
