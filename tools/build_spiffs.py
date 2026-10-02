"""Stage data/ into data_build/ with the text assets gzipped.

ESPAsyncWebServer serves a `.gz` sibling automatically and sets Content-Encoding, so
compressing HTML, CSS, JS and SVG before upload costs nothing at runtime. On this
project it takes a cold load from about 141 KB to about 75 KB.

Sources stay editable in data/ - this only writes the staging directory that
platformio.ini points at, so `python -m http.server --directory data` still works for
local work.

Only the compressed copy is staged. Shipping both wastes flash, and some
ESPAsyncWebServer versions will serve whichever they find first, which silently undoes
the win and makes a stale asset very confusing to debug.

The main page is also inlined: its stylesheet and script are folded into index.html,
because each request to the board costs a flat 0.08 s whatever its size and a cold load
is mostly that overhead. Sources stay as three hand-editable files. The OTA page keeps
its own separate files - it has to work while the main page's assets are being replaced.

Run before uploading the filesystem:

    python tools/bump_spiffs_version.py     # version + cache-bust first
    python tools/build_spiffs.py
    pio run -t uploadfs
"""

import gzip
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "data"
OUT = ROOT / "data_build"

# Already-compressed formats gain nothing and cost CPU to decode twice.
COMPRESS = {".html", ".css", ".js", ".svg", ".json", ".txt"}

# Pages whose local stylesheet and script are folded in, and so not staged on their own.
INLINE_PAGES = {"index.html"}
LINK_RE = re.compile(r'<link rel="stylesheet" href="(?P<file>[\w./-]+\.css)(?:\?v=[^"]*)?"\s*/?>')
SCRIPT_RE = re.compile(r'<script src="(?P<file>[\w./-]+\.js)(?:\?v=[^"]*)?"></script>')

# Below this, the gzip header and the extra flash block cost more than they save.
MIN_BYTES = 512


def inline_assets(html, inlined):
    def embed(tag):
        def sub(match):
            asset = SRC / match.group("file")
            inlined.add(asset)
            return "<%s>\n%s</%s>" % (tag, asset.read_text(encoding="utf-8"), tag)
        return sub

    html = LINK_RE.sub(embed("style"), html)
    return SCRIPT_RE.sub(embed("script"), html)


def main():
    if not SRC.is_dir():
        raise SystemExit("no data/ directory at %s" % SRC)

    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)

    raw_total = 0
    out_total = 0
    rows = []

    files = [p for p in sorted(SRC.rglob("*")) if not p.is_dir()]
    inlined = set()
    pages = {}
    for path in files:
        if path.name in INLINE_PAGES:
            pages[path] = inline_assets(path.read_text(encoding="utf-8"), inlined).encode("utf-8")

    for path in files:
        if path in inlined:
            continue
        rel = path.relative_to(SRC)
        dest = OUT / rel
        dest.parent.mkdir(parents=True, exist_ok=True)

        raw = pages.get(path) or path.read_bytes()
        raw_total += len(raw)

        if path.suffix.lower() in COMPRESS and len(raw) >= MIN_BYTES:
            packed = gzip.compress(raw, 9)
            # Compression that does not pay for itself is just a decode cost.
            if len(packed) < len(raw):
                dest.with_suffix(dest.suffix + ".gz").write_bytes(packed)
                out_total += len(packed)
                rows.append((str(rel), len(raw), len(packed)))
                continue

        dest.write_bytes(raw)
        out_total += len(raw)
        rows.append((str(rel), len(raw), len(raw)))

    width = max(len(r[0]) for r in rows)
    print("%-*s %10s %10s" % (width, "asset", "raw", "staged"))
    print("-" * (width + 22))
    for name, raw, packed in rows:
        mark = "" if raw == packed else "  gz"
        print("%-*s %9.1fK %9.1fK%s" % (width, name, raw / 1024.0, packed / 1024.0, mark))
    print("-" * (width + 22))
    print("%-*s %9.1fK %9.1fK   (%.0f%% smaller)"
          % (width, "TOTAL", raw_total / 1024.0, out_total / 1024.0,
             100.0 * (raw_total - out_total) / raw_total))
    print("\nStaged into %s - point uploadfs at it, not data/." % OUT.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
