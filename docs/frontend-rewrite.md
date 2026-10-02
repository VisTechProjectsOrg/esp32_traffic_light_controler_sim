# Frontend rewrite

A plan for replacing `data/` with something that loads quickly off the ESP32, keeping every feature
that exists today. Written to be picked up cold in a later session.

## Why

The page is served from SPIFFS on an ESP32 over WiFi. That is the whole constraint: every kilobyte is
read off flash by a 240 MHz MCU and pushed through a single-radio TCP stack, so payload size is
latency, directly.

Two numbers matter here and they are not the same number.

**Cold load is 141 KB.** That is what a browser actually transfers on a first visit - the browser only
fetches what `index.html` references. Gzipped it would be **72 KB**.

| Cold load | Raw | Gzipped |
|---|---|---|
| HTML + CSS + JS | 84.8 KB | **18.8 KB** |
| Images (car, one lamp, icons) | 56.0 KB | 55.7 KB |
| **Total** | **140.8 KB** | **72.3 KB** |

**SPIFFS footprint is 823 KB.** That is flash space, and it is dominated by cat mode at 477 KB - 58%
of everything, with `all_on_cat.png` alone at 213 KB. None of it transfers unless cat mode is toggled.

Keep the two apart when deciding what to do:

- To make **loading faster**, gzip the text. 84.8 KB becomes 18.8 KB, saving 66 KB on every cold load,
  for a build step and no firmware change. Nothing else comes close. After that, images are what is
  left (53 of the remaining 72 KB), and the single largest is `is250.webp` at 31.6 KB - the car
  picture, bigger than the traffic light.
- To **reclaim flash**, re-encode the cat images. That is where the 477 KB lives. It will not make
  the page load any faster for anyone who never turns cat mode on.

Gzip helps text only. The images are already compressed formats, so gzipping them saves nothing -
which is why the image column above barely moves.

## Targets

- Cold load under 80 KB transferred.
- `data/` footprint under 300 KB.
- Cold load on a phone over WiFi under 2 s.
- No feature lost. The list below is the contract.
- Still editable by hand - no bundler, no framework, no build toolchain beyond a gzip script.

## Feature inventory

Everything the current UI does. A rewrite is done when all of this works again.

### Live display
- Traffic light showing red / yellow / green / all-on / all-off, driven by the `state` websocket key.
- **Cat mode** - the same five states with cat artwork, toggled by `theme_mode`.
- Pedestrian head - hand, walking person, two-digit countdown, driven by `ped_state`. Already inline
  SVG; carry it over as is.
- Car distance visualisation - the IS250 driving toward a wall with a tick scale, from `distance`.
- Sensor diagnostics card - state, raw vs filtered, strength, learned baseline, plus a relearn button.
- Reconnect popup when the websocket drops.
- Boot splash holding the page until first config and state land, 8 s timeout.

### Controls
- Blink mode toggle, and blink colour select: all / red / yellow / green / random.
- Cat mode toggle.
- Settings dialog:
  - light timings - red, yellow, green
  - distance zones - danger, warning, max, zone persistence, sensor enable
  - pedestrian - walk, flashing hand, steady hand, chain phase, follow-cycle
  - live movement-total readout, saying when connected mode will hold the vehicle phase
  - firmware and SPIFFS version labels
  - link to the OTA page
- OTA page - its own HTML/CSS/JS trio, drag-and-drop firmware and SPIFFS upload.
- The console easter egg in `effect.js`. 13.5 KB of rainbow CSS for one `console.log`. Keep the joke,
  generate the gradient in a loop instead of shipping it as a literal.

### Server contract - do not change without changing the firmware

Routes: `/`, `/get_config`, `/set_config`, `/get_current_state`, `/blink_mode`, `/toggle_light_mode`,
`/toggle_theme_mode`, `/ped_control`, `/proximity_control`, `/test_mode`, `/set_output`,
`/update_firmware`, `/reset_ota_state`, `/update`.

Websocket keys the UI parses: `state`, `light_mode`, `theme_mode`, `blink_color`, `ped_state`,
`distance`, `sensor_temp`, `proximity`, `proximity_zone`, `proximity_baseline`, `sensor_disconnected`.

`notifyAllClientsDistance()` sends `sensor_temp`, not `temp`. That exact key has already been broken
once in this project.

## Approach

### 1. Gzip everything text (do this first, independent of the rest)

Extend `tools/bump_spiffs_version.py`, or add a sibling, to write `.gz` beside every `.html`, `.css`
and `.js` in `data/` at upload time. ESPAsyncWebServer picks the compressed file up on its own. No
firmware change.

Keep the plain files too while verifying, then drop them - a stale uncompressed sibling will be
served in preference on some versions and silently undo the win.

### 2. Cat mode: flash space, not load speed

477 KB for one joke mode. Worth fixing, but be clear that this reclaims flash and speeds up the first
cat-mode toggle - it does not touch cold load. Options:

- **Re-encode as WebP.** Same artwork, same behaviour, typically 70-80% smaller. ~100 KB total. No
  code change beyond the file extension. Lowest risk, keeps the joke intact.
- **Load on demand.** Ship the normal light always, fetch cat images only when cat mode is first
  switched on. Cold load never pays for them. Slight delay on first toggle.
- **Both.** WebP *and* lazy - about 100 KB that most sessions never request.

Do not drop cat mode. It is in the readme as a headline feature.

### 3. Normal traffic light to inline SVG

Three lamps in a housing - the ped head in `data/index.html` is the pattern to follow. Removes 77 KB,
scales cleanly, themeable, and makes the lit state a class toggle instead of an image swap. The lamp
states become `red` / `yellow` / `green` / `all_on` / `all_off` exactly as now, so `updateTrafficLight()`
keeps its signature.

Cat mode still swaps in an image, so both paths must coexist: SVG shown in normal mode, `<img>` in cat
mode.

### 4. Collapse the file set

Five text files become two. `car-distance.js` and `car-distance-style.css` are small and only used on
the one block; fold them in. One request instead of five matters more here than on a normal host,
because each is a separate SPIFFS read and the ESP32 serves few connections at once.

The OTA page keeps its own trio - it has to work when the main page's assets may be mid-replacement.

### 5. Splash waits for assets

Currently it waits on `/get_config` and `/get_current_state`. Make it also wait for the images it
needs, so a slow SPIFFS read cannot show a half-painted page. Check `img.complete` as well as binding
`load`, because a fast serve can fire the event before the listener attaches.

## Order of work

Each step is independently shippable and independently revertable.

1. **Gzip at upload.** 141 KB cold load to 75 KB. Biggest single win, no firmware change.
2. **Shrink `is250.webp`.** At 31.6 KB it is the largest thing on a cold load, and it is decorative.
3. **Traffic light to SVG.** About 15 KB off the cold load, and it gzips where a PNG does not.
4. Merge the text files, splash waits on assets.
5. Cat images re-encoded - flash footprint only, do it whenever.
6. Restyle, once the structure is settled.

Step 1 is half the win on its own. Do not start at step 6.

## Verification

- Cold load under 80 KB transferred, measured in devtools with cache disabled.
- `data/` footprint under 300 KB.
- Every item in the feature inventory exercised by hand against real hardware.
- Cold load timed on a phone with cache cleared, before and after.
- `python tools/bump_spiffs_version.py` still rewrites the asset URLs, and the HTML is still sent
  `no-cache` while assets stay on a long `max-age`. That pairing is what makes the cache safe.
- Three widths: 1280, 800, 360. No horizontal page scroll at any of them.
- The websocket keys above still parse - check the browser console for the "Unknown data received"
  branch firing.
