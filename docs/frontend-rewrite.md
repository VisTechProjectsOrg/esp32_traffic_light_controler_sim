# Frontend rewrite

A plan for replacing `data/` with something that loads quickly off the ESP32, keeping every feature
that exists today. Written to be picked up cold in a later session.

## Why

The page is served from SPIFFS on an ESP32 over WiFi. That is the whole constraint: every kilobyte is
read off flash by a 240 MHz MCU and pushed through a single-radio TCP stack, so payload size is
latency, directly.

Today `data/` is **823 KB**. Where it goes is not where you would guess:

| | Size | Share |
|---|---|---|
| **Cat mode images** | **477 KB** | **58%** |
| Other images (404 cat, firmware, car, icons) | 168 KB | 20% |
| HTML + CSS + JS | 102 KB | 12% |
| Normal traffic light PNGs | 77 KB | 9% |

Two things follow.

**Cat mode is the payload.** `all_on_cat.png` alone is 213 KB - larger than every stylesheet and
script combined. Redrawing the *normal* traffic light as SVG, which was the original idea, saves
77 KB and leaves 58% of the problem untouched.

**Nothing is compressed.** There is no gzip anywhere in `data/`, and ESPAsyncWebServer serves a
`.gz` sibling automatically when one exists, with the right `Content-Encoding`. Text compresses
around 75%, so the 102 KB of HTML/CSS/JS becomes roughly 25 KB for the cost of a build step. This is
free and should happen regardless of whether the rewrite goes ahead.

## Targets

- `data/` under 250 KB total.
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

### 2. Cat mode: the actual decision

477 KB for one joke mode. Options, in the order I would consider them:

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

1. Gzip at upload. Measure before and after.
2. Cat images to WebP. Measure.
3. Traffic light to SVG.
4. Merge the text files, splash waits on assets.
5. Restyle, once the structure is settled.

Steps 1 and 2 are most of the win and touch almost no code. Do not start at step 5.

## Verification

- `data/` total under 250 KB.
- Every item in the feature inventory exercised by hand against real hardware.
- Cold load timed on a phone with cache cleared, before and after.
- `python tools/bump_spiffs_version.py` still rewrites the asset URLs, and the HTML is still sent
  `no-cache` while assets stay on a long `max-age`. That pairing is what makes the cache safe.
- Three widths: 1280, 800, 360. No horizontal page scroll at any of them.
- The websocket keys above still parse - check the browser console for the "Unknown data received"
  branch firing.
