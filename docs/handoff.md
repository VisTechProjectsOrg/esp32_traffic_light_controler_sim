# Handoff

State as of 2026-10-02. Everything described is committed and pushed to `dev`.

## Read this first

**mDNS costs ~1.02 s per request.** Measured, repeatably:

```
by IP    10.0.0.102          0.00004 s DNS   0.02-0.05 s total
by name  trafficlights.local 1.02    s DNS   1.19      s total
```

An 11-request cold load over `trafficlights.local` spends up to 11 seconds in name lookup. This is
the dominant cause of the device feeling slow, by an order of magnitude over payload size. It also
fails outright often enough to leave the settings dialog empty and the version labels at `v0.0`,
because `/get_config` never resolves.

Use the IP while working. Fixing mDNS properly, or dropping it for a static lease, is the single
highest-value performance task left and nothing else comes close.

## Hardware

Flashed and running. ESP32 on COM7 (CH340), station mode, **10.0.0.102**.

Verified live on the board:

- All pages serve 200 and gzipped.
- `/ped_control` returns `walk 7, fdw 15, dw 3, phase_total 25, chained true, chain_phase red`.
- `/proximity_control` returns state (sensor currently disabled).
- Serial shows `Vehicle phase held to 25000ms for the pedestrian movement (configured 5000ms)` - the
  connected-mode floor working on real hardware.

**Nothing downstream of the ESP32 has been tested.** No relays, no signal heads, no mains. The entire
pedestrian phase, the countdown feed and the proximity rewrite have only ever run against serial
output and HTTP.

## What was built this session

**Pedestrian signal.** WALK to flashing-DON'T-WALK to steady, on its own channels, with the countdown
module fed separately. `src/ped.cpp`.

**Connected and disconnected modes.** Chained, the pedestrian clearance raises a floor under the
vehicle phase, so a 5 s red is held to 25 s rather than the crossing being cut short. Free-running,
the ped head loops on its own clock and the vehicle cycle keeps its fast timings. The reason the
vehicle phase stretches instead of the crossing compressing is that any compression lands on FDW, and
FDW length is literally the number the countdown module learns.

**Clock and output ownership separated.** `cycleLights()` now always advances; proximity only
suppresses its writes and forces a repaint when it lets go. Previously the cycle froze whenever
proximity held the lamps.

**Distance sensing rewritten** around a learned baseline with an explicit state machine, 20 Hz
sampling into a median. `tools/proximity_sim.py` replays scenarios against the real constants.

**main.cpp split** from 1253 lines into state / signals / traffic / ped / proximity / webserver.

**Frontend**: pedestrian head drawn as inline SVG and wired to the live phase, sensor diagnostics
card, boot splash, cache busting, and gzip staging.

## Known broken

**Settings dialog layout.** Two boxes on the top row, the Pedestrian box orphaned below-left with a
large empty area beside it, and the live phase readout sitting mid-form between two fields instead of
at the end. Needs a proper grid.

**Empty config fields and `v0.0` labels.** A symptom of the mDNS failure above, not a separate bug -
confirm against the IP before chasing it.

**`src/config.h` is tracked despite being in `.gitignore`**, so the WiFi credentials are in history on
a **public** repo. Rotating the password is the fix; history scrubbing does not help once public.

## Open work, in order

1. **mDNS.** Worth ~1 s per request. Everything else is noise beside it.
2. **Settings dialog layout.**
3. **Merge the frontend text files.** Each request costs a flat 0.08 s by IP regardless of size, so
   11 requests to 6 saves about 0.4 s. See `docs/frontend-rewrite.md` for the measured breakdown and
   the full feature inventory a rewrite has to preserve.
4. **Bench-test the wiring** with `tools/signal_test_gui.py` before mains. See `hardware/README.md`.
5. Decouple the subsystems from the websocket; drop the `cycleLights()` call out of `handleRoot()`.

## Gotchas that will bite

- **Explicit `beginResponse(SPIFFS, ...)` does not find a `.gz` sibling.** `serveStatic` does. The
  build stages only the compressed copy, so hand-served pages go through `spiffsPage()`. This already
  broke the root handler once.
- **`notifyAllClientsDistance()` sends `sensor_temp`, not `temp`.** Already broken once.
- **FDW duration is the countdown's learned value.** Never truncate it. Skip the phase instead.
- **The relay boards are active low** and the inversion lives only in `signals.cpp`, except
  `traffic.cpp` blink mode, which still writes pins directly.
- **GPIO 12 is a strapping pin** - vehicle red was moved to 21 for that reason.
- Upload flow is `bump_spiffs_version.py`, then `build_spiffs.py`, then `pio run -t uploadfs`.
  `platformio.ini` points `data_dir` at `data_build/`.
