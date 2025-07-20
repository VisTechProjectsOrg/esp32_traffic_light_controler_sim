import json

# Read versions from version.json
with open("version.json", "r") as f:
    version = json.load(f)

# Match version strings
firmware_version = version.get("firmware", "unknown")
spiffs_version = version.get("spiffs", "unknown")

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
