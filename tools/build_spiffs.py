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

Run before uploading the filesystem:

    python tools/bump_spiffs_version.py     # version + cache-bust first
    python tools/build_spiffs.py
    pio run -t uploadfs
"""

import gzip
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "data"
OUT = ROOT / "data_build"

# Already-compressed formats gain nothing and cost CPU to decode twice.
COMPRESS = {".html", ".css", ".js", ".svg", ".json", ".txt"}

# Below this, the gzip header and the extra flash block cost more than they save.
MIN_BYTES = 512


def main():
    if not SRC.is_dir():
        raise SystemExit("no data/ directory at %s" % SRC)

    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)

    raw_total = 0
    out_total = 0
    rows = []

    for path in sorted(SRC.rglob("*")):
        if path.is_dir():
            continue
        rel = path.relative_to(SRC)
        dest = OUT / rel
        dest.parent.mkdir(parents=True, exist_ok=True)

        raw = path.read_bytes()
        raw_total += len(raw)

        if path.suffix.lower() in COMPRESS and len(raw) >= MIN_BYTES:
            packed = gzip.compress(raw, 9)
            # Compression that does not pay for itself is just a decode cost.
            if len(packed) < len(raw):
                dest.with_suffix(dest.suffix + ".gz").write_bytes(packed)
                out_total += len(packed)
                rows.append((str(rel), len(raw), len(packed)))
                continue

        shutil.copy2(path, dest)
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
