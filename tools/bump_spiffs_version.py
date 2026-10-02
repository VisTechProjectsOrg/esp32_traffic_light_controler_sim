"""Bump the SPIFFS version and cache-bust the assets that reference it.

Assets are served with a long max-age so a reload does not re-download 800K over
WiFi. That is only safe if a new SPIFFS build changes the URLs, otherwise browsers
keep serving the old CSS and JS after an OTA. This rewrites the ?v= query on every
local stylesheet and script in the data/ HTML files to match version.txt.

The HTML itself is sent with no-cache by the firmware, so the new URLs are always
picked up immediately.

Run before uploading a SPIFFS image:

    python tools/bump_spiffs_version.py           # 0.1.8 -> 0.1.9
    python tools/bump_spiffs_version.py 0.2.0     # set explicitly
    python tools/bump_spiffs_version.py --check   # verify, change nothing
"""

import re
import sys
from pathlib import Path

DATA = Path(__file__).resolve().parent.parent / "data"
VERSION_FILE = DATA / "version.txt"
ASSET_RE = re.compile(
    r'(?P<attr>(?:href|src)=")(?P<file>[\w./-]+\.(?:css|js|png|webp|svg))(?:\?v=[^"]*)?(?P<end>")')


def read_version():
    return VERSION_FILE.read_text(encoding="utf-8").strip()


def bump(version):
    parts = version.split(".")
    if not parts[-1].isdigit():
        raise SystemExit("cannot bump non-numeric version: %s" % version)
    parts[-1] = str(int(parts[-1]) + 1)
    return ".".join(parts)


def rewrite(html_path, version, check):
    text = html_path.read_text(encoding="utf-8")

    def sub(match):
        # leave absolute and remote URLs alone
        return "%s%s?v=%s%s" % (match.group("attr"), match.group("file"), version, match.group("end"))

    updated = ASSET_RE.sub(sub, text)
    if updated == text:
        return 0

    count = len(ASSET_RE.findall(text))
    if not check:
        html_path.write_text(updated, encoding="utf-8")
    return count


def main():
    args = [a for a in sys.argv[1:]]
    check = "--check" in args
    if check:
        args.remove("--check")

    current = read_version()
    version = args[0] if args else (current if check else bump(current))

    changed = 0
    for html in sorted(DATA.glob("*.html")):
        changed += rewrite(html, version, check)

    if check:
        print("version.txt = %s; %d asset reference(s) would change" % (current, changed))
        return 1 if changed else 0

    VERSION_FILE.write_text(version + "\n", encoding="utf-8")
    print("SPIFFS version %s -> %s, rewrote %d asset reference(s)" % (current, version, changed))
    print("Remember to upload the SPIFFS image, not just the firmware.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
