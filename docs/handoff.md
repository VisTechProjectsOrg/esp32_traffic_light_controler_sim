# Handoff

State as of 2026-10-09. Committed on `dev`.

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

Diagnosed - it is not a firmware bug and cannot be fixed from the ESP32:

```
unicast DNS (asks the router)   716 ms   no answer
LLMNR / multicast              1019 ms   10.0.0.102
default (tries both)           1050 ms   10.0.0.102
```

Windows queries the router first, waits ~716 ms for an answer no DNS server can give - `.local` is
reserved for multicast - then falls back to multicast, which takes a further second because Windows
waits out a fixed response window and the ESP32's responder is slow, sharing the WiFi task.

**Fix it on the router**: give the board a static DHCP reservation with a hostname there. It then
resolves over ordinary unicast DNS in a millisecond or two, for every device on the network, and
survives reboots. A hosts-file entry works too but only on one machine. Keep mDNS enabled either way
as a way to find the board when its address is unknown; just do not make it the daily path.

**Mitigated in firmware.** `handleRoot()` now bounces Windows clients that arrive by name to the IP
(302), gated on `REDIRECT_MDNS_TO_IP` in `config.h`. The redirect changes the origin, so assets, API
calls and the websocket afterwards resolve nothing - the name is looked up once per navigation rather
than once per request. Verified on hardware: Windows UA by name redirects and lands in 1.23 s, the
same UA by IP does not redirect at all (no loop), and a macOS UA keeps the friendly name because
Apple platforms resolve `.local` natively in milliseconds.

**AP mode is the real deployment** - in the garage the phone connects to the device directly, and
there is no router to put a reservation on. So the captive DNS is the fix there, not a workaround:
in AP mode the device is its own DHCP server, hands out itself as the DNS server, and now answers
every query with its own address. Any hostname typed into a browser reaches it, with no IP to
remember and no mDNS to wait on. `WifiManager::captivePortalLoop()` is pumped from `loop()` under
`#ifdef AP_SSID`.

**Partly verified on hardware, 2026-10-02.** Flashed the AP build and joined it from the PC:

```
resolve lights.example   -> AP address   45 ms
resolve garage.test      -> AP address    5 ms
resolve www.google.com   -> AP address   11 ms
GET http://lights.example/        200  0.08 s
GET http://garage.test/get_config 200  0.03 s
```

So the AP comes up, the captive DNS answers every name in milliseconds, and the web server
serves by any hostname. **What is still unproven is the DHCP half**: the test PC has a static
address on its WiFi adapter, so it never took a lease and had to be pointed at the DNS server
by hand (with the AP temporarily moved onto the PC's subnet). Nobody has yet watched a client
receive the device as its DNS server from the lease. That needs a phone: flip `config.h` to
`AP_SSID`, join, type any hostname. Also watch for the phone's "no internet" prompt - the
wildcard DNS answers its connectivity probe with our 404 page, and some phones respond by
dropping back to mobile data unless told to stay connected.

The AP is open unless `AP_PASS` is uncommented. For a device that will switch mains, set one.

For station mode on a network you control, a router reservation is still the better answer - but that
is not available here.

## Hardware

Flashed and running. ESP32 on COM7 (CH340), station mode, **10.0.0.102**.

Verified live on the board:

- All pages serve 200 and gzipped.
- `/ped_control` returns `walk 7, fdw 15, dw 3, phase_total 25, chained true, chain_phase red`.
- `/proximity_control` returns state (sensor currently disabled).
- Serial shows `Vehicle phase held to 25000ms for the pedestrian movement (configured 5000ms)` - the
  connected-mode floor working on real hardware.

**Wired and running on mains, 2026-10-09.** All seven relays were confirmed one lamp at a time from
the serial console, then the normal cycle was left running: vehicle lamps, WALK, flashing hand and
the countdown, which learned and counted to 0. Pins as wired: red 25, yellow 27, green 26, ped hand
32, ped walk 33, countdown hand 14, countdown walk 13. `hardware/README.md` has the reasons.

That board is a different one from the bench board above: MAC `EC:C9:FF:E2:F7:B4`, on COM5, in AP
mode at 192.168.4.1. The proximity rewrite is still untested on hardware - the sensor is compiled
out, because its UART pins (25/26) now carry relays.

**Serial console** (`src/bench.cpp`, 115200 baud, `help` lists it): toggle or flash any relay by
name or strip position, `flash` for the clearance interval on both ped heads, `set fdw 5` for the
pedestrian times, `run` to hand the outputs back to the cycle. It shares its held state with
`/test_mode` and `/set_output`.

**The partition table changed.** With the pedestrian code in, the image is 1.35 MB (1.38 MB with
FastLED) and no longer fits the default 1.25 MB app slot. `partitions.csv` gives each app slot
1.5 MB and SPIFFS 896 KB. A board flashed over USB picks it up; a board still on the old layout
cannot take these builds over OTA and has to be reflashed by cable once.

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

**Frontend rewritten** (SPIFFS 0.1.15), phone first. See `docs/frontend-rewrite.md` for what
changed and the measurements. In short: one inlined page instead of five text files, the
traffic light drawn as SVG, cat and car artwork re-encoded, a settings sheet that is one
column at every width. Cold load went from 11 requests and about 1.8 s to 4 requests and
about 0.3 s; the staged image went from about 750 KB to 84 KB.

**Pedestrian timing has two modes in the UI.** On its own timer: the head loops on walk +
countdown + solid hand. With the light (`chained` + `fit`): the crossing runs under the
chosen phase, WALK rests until the countdown can end exactly with the phase, and the solid
hand falls on the phases that follow. The settings form caps the countdown at that light's
length (lowering it, highlighted, if the light is shortened), so the light is never
stretched; the firmware skips a crossing only if the light is shorter than the FDW. The old
hold path (`chained` without `fit`: the vehicle phase is held to walk + FDW) is still in
`traffic.cpp` but nothing in the UI selects it any more - delete it or keep it for the API.
Walk may be 0 in own-timer mode; the movement then starts at the flashing hand.

**Distance sensing ignores everything past the max.** The Green zone setting
(`distance_max`) is now the farthest a target can be picked up, so with the garage door open
nobody in the driveway can take over the lamps; before, anything about 2 ft inside the
learned background could, out to 18-20 ft. The entry-gap check is measured from that same
boundary, so a short max still accepts a real car. `tools/proximity_sim.py` has the
door-open scenarios.

**Sensor presence is detected** from the TF-Luna's unprompted 100 Hz stream: no bytes for 1 s
means no sensor (`lidarConnected()`), reported as `distance_sensor_connected` in
`/get_config`, `connected` in the diagnostics, and the `sensor_disconnected` websocket key.
The settings switch cannot be turned on with nothing attached. This also fixed a stall:
`TFMPlus::getData()` waits up to 1 s for a frame, so an enabled-but-unplugged sensor froze the
loop on every sample; it is now only called when bytes are waiting. The bench board reports
no sensor, so detection of a *connected* sensor is still unverified.

**Written 2026-10-08.** Board identity is verified on hardware (boot line and `id`). The approach
setting still only compiles - the sensor is off:

- **Approach setting** (`approach_min`, pref `appr_min`, 0.5-5 ft, default 2): how far a
  target must close before it counts as a car. Replaces the `APPROACH_MIN_FT` constant; the
  field is in Settings under Distance sensor.
- **Board identity**: `/identify`, an `IDENTITY {json}` line at boot, and the same line in
  answer to `id` on the serial port (project, firmware, SPIFFS, MAC, mode, IP). Ask for it
  before flashing - COM ports move, and another project's ESP has already turned up on one.

SPIFFS 0.1.23 is on the garage board.

## Known broken

**The WiFi password is in public git history.** `src/config.h` is no longer tracked (copy
`src/config.example.h` to start), but every commit before that still has it, and the
v0.1.15 release binaries were built from it. Rotating the password on the router is the only
fix; history scrubbing does not help once public.

## Open work, in order

1. **Phone test of AP mode** - the DHCP-hands-out-DNS half, above.
2. **Rotate the WiFi password** that is in git history.
3. **Give the TF-Luna new UART pins** in `LidarHelper.h` and turn `DISTANCE_SENSOR_ENABLED` back on.
   Pick pins with a pull-up available - 34-39 have none, and a floating RX would look like a sensor.
4. **Validate `/set_config` and `/ped_control` in the firmware.** The UI bounds every field, but
   the handlers store whatever arrives, so a hand-made request can still save a zero delay.
5. Decouple the subsystems from the websocket; drop the `cycleLights()` call out of `handleRoot()`.
6. The sensor card has only been driven with injected messages - the sensor is disabled on the
   bench board. Exercise it against the real TF-Luna.

7. Read the main fuse value off the part and record it. It sits on the dimmer board, feeds the
   whole build, and was deliberately fitted small (an amp or two); the dimming is not used.

## Gotchas that will bite

- **Explicit `beginResponse(SPIFFS, ...)` does not find a `.gz` sibling.** `serveStatic` does. The
  build stages only the compressed copy, so hand-served pages go through `spiffsPage()`. This already
  broke the root handler once.
- **`notifyAllClientsDistance()` sends `sensor_temp`, not `temp`.** Already broken once.
- **FDW duration is the countdown's learned value.** Never truncate it. Skip the phase instead.
- **The relay boards are active low** and the inversion lives only in `signals.cpp`, except
  `traffic.cpp` blink mode, which still writes pins directly.
- **`loop()` takes `now` before `cycleLights()` runs.** Anything started inside it is stamped
  with a later `millis()`, so `now - start` wraps to a huge number. This ended a new ped
  interval the instant it began; `updatePedSignal()` takes its own reading now.
- **Walk may be 0.** The movement then starts at the flashing hand, never passing through
  WALK, so the relay does not click.
- **GPIO 12 is a strapping pin** - a relay input on it boot-loops the board and blocks flashing.
  34 and 35 are input-only. Both cost time on the bench; see `hardware/README.md`.
- Upload flow is `bump_spiffs_version.py`, then `build_spiffs.py`, then `pio run -t uploadfs`.
  `platformio.ini` points `data_dir` at `data_build/`.
- **`build_spiffs.py` inlines `style.css` and `script.js` into the staged `index.html`**, so
  those two URLs 404 on the board. Edit the three source files; never reference the CSS or JS
  from anywhere else.
- **The page pings the websocket every 5 s** purely so a rebooted board is noticed. The
  firmware ignores incoming text; if it ever starts parsing it, `ping` must stay harmless.
