const fs = require('fs');

const versionPath = './version.json';
const readmePath = './README.md';
const markerStart = '<!--VERSIONS_START-->';
const markerEnd = '<!--VERSIONS_END-->';

const version = JSON.parse(fs.readFileSync(versionPath, 'utf8'));
const readme = fs.readFileSync(readmePath, 'utf8');

const versionBlock = `
${markerStart}
## 🔢 Firmware Versions

- **Firmware:** \`v${version.firmware}\`
- **SPIFFS:** \`v${version.spiffs}\`
${markerEnd}
`;

const updated = readme.replace(
  new RegExp(`${markerStart}[\s\S]*${markerEnd}`),
  versionBlock.trim()
);

if (updated !== readme) {
  fs.writeFileSync(readmePath, updated, 'utf8');
  console.log('README updated with latest versions.');
} else {
  console.log('No version change. Skipping.');
}
