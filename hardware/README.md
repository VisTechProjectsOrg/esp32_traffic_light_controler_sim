# Hardware notes

Real signal hardware driven by this controller. Reference photos live in `hardware/photos/`
(gitignored - large phone photos, kept local only).

## Pedestrian signal modules (EOI / Excellence Opto Inc.)

Both are 12" x 12" square modules, mains powered, from the same 11-2016 lot.

| | Hand/Man combo | Countdown |
|---|---|---|
| Model | TRP-C30DD2C3(H-cETL) | TRP-O30D32E2(cETL) |
| Description | 12"x12" Pedestrian Signal - COMBO | 12"x12" Pedestrian Signal (2-digit countdown) |
| Input | 80-135 VAC, 50/60 Hz | 80-135 VAC, 50/60 Hz |
| Power | HAND 7.5 W / MAN 7.5 W | 9 W, 10.5 VA |
| Current @ 120 VAC | ~62 mA per symbol | ~88 mA |
| Lot / S/N | A512-1611009 / 1611009A00283 | A512-1611017 / 1611017A00331 |
| Approvals | ETL recognized, CAN/CSA C22.2 No. 207 | ETL recognized + Intertek LED Traffic Signal Modules Certification Program |
| IC Ver | - | C0515 |

EOI publishes no public datasheet for these part numbers; the table above is transcribed from the
module nameplates. Everything below follows the standard North American (ITE / MUTCD) pedestrian
signal conventions that these modules are built to.

## Field wiring

Module pigtails: **white, blue, orange**. Housing cable: **white, blue, red, orange** (+ bare/green
ground).

Standard ITE pedestrian colour code:

| Wire | Function | Switched? |
|---|---|---|
| White | AC neutral, common to both symbols | **Never switch neutral** |
| Orange | DON'T WALK (hand) hot | yes |
| Blue | WALK (man) hot | yes |
| Green / bare | Chassis ground | no |
| Red | vehicle-head RED in the housing cable (not part of the ped module) | yes (already wired) |

**Verify before wiring mains.** Colour conventions vary by installer. With the head unpowered, ring
out each pigtail to the module terminals, or bench-test one symbol at a time from a fused cord with
only white + one colour connected.

The original housing cable can be cut back or replaced entirely - nothing in the modules depends on
it. Simplest bench wiring is to land the module pigtails straight onto the SSR board: orange and blue
each to their own channel output, all whites bundled to a single neutral, green to ground. Keep the
countdown's orange and blue spliced in parallel with the combo module's so it sees the same phase
transitions.

The two hot inputs are mutually exclusive - the module is designed for exactly one symbol energized
at a time. Energizing orange and blue together lights both symbols and draws 15 W; firmware must
prevent it.

## Countdown module behaviour

The countdown taps the **same** WALK and DON'T WALK hots, wired in parallel with the combo module.
It needs no relay channel of its own.

### How the two modules share two wires

Both modules have the identical three pigtails (white / blue / orange). They splice together colour to
colour, so the whole ped head - symbols and countdown - runs on **two switched channels total**:

```
                        ┌─────────────────────┐
  SSR ch 5 ──── orange ─┬─────────────────────┤  HAND / MAN combo
  (DON'T WALK)          │                     │  TRP-C30DD2C3
                        │   ┌─────────────────┤
  SSR ch 6 ──── blue ───┼───┬─────────────────┘
  (WALK)                │   │
                        │   │ ┌───────────────┐
  neutral ───── white ──┼───┼─┤               │  COUNTDOWN
                    ────┴───┴─┤               │  TRP-O30D32E2
                              └───────────────┘
```

There is no data wire and no separate FDW wire. The countdown just listens to the same two AC hots and
works out the phase from what it sees:

| What it sees | What it concludes |
|---|---|
| blue energized | WALK - display blank |
| orange **chopping on/off at 1 Hz** | FDW - count down |
| orange **steady on** | solid DON'T WALK - display blank |

That last distinction is the whole trick: FDW and steady DON'T WALK are the *same wire*, and the module
tells them apart purely by whether the AC is flashing or continuous. This is why the 1 Hz flash rate
matters - it is not cosmetic, it is the signal that puts the countdown into counting mode.

The wire nuts visible in `photos/20260923_223046.jpg` are this parallel splice as it came from the
factory.

### Self-timing

The countdown needs **no programming, no data input and no DIP switches**. It infers everything from
the AC on those two hots:

- On power-up it holds the display **blank for one pedestrian cycle** (spec allows up to two) while it
  measures the flashing-DON'T-WALK (FDW) interval.
- From the next cycle on it counts that interval down to zero, reaching 0 as FDW ends and steady
  DON'T WALK begins.
- It keeps monitoring. If the FDW duration changes it relearns automatically, costing one blank cycle.
  Changing the timing from the web UI is therefore self-correcting, not a fault.
- A >2 s power interruption wipes the stored value and forces a fresh learn cycle.

**The FDW duration is the only control surface.** There is no way to command an arbitrary number - if
you want it to show 15, make the FDW phase 15 seconds long. The count is always
`round(FDW duration in seconds)`.

Per MUTCD the countdown displays **only during FDW** - blank during WALK and during steady DON'T WALK.
The FDW flash itself is 1 Hz at ~50% duty; the existing `blinkInterval` default of 1000 ms is already
a full 1 Hz period, so 500 ms on / 500 ms off is the correct target.

A free-running cycle is required for any of this to work. Manually toggling WALK/DON'T WALK from the
web UI without a consistent FDW phase leaves the display blank - again, working as designed.

Ped phase order: **WALK (solid blue)** -> **FDW (orange flashing 1 Hz)** -> **DON'T WALK (solid orange)**.

### WALK is variable, FDW is fixed

These two intervals are set by completely different things, which is why the countdown works at all:

- **WALK** is a traffic-flow number. It can be any length, can be extended, and can be re-served. It
  does not have to be consistent and the countdown module does not care about it.
- **FDW** (the pedestrian clearance interval) is a walking-speed number - real controllers compute it
  as crossing distance / 3.5 ft per second. It is a fixed property of the intersection, so it is the
  same every cycle. That invariance is exactly what the countdown module relies on.

### Ped recycle

On a long vehicle green, a real controller that gets a fresh ped call after FDW has already run can
**recycle the ped phase** - go back to WALK and run the whole thing again before the green ends:

```
GREEN ──────────────────────────────────────────────────────────
 ped:  WALK ──► FDW ──► DW ──► WALK ──► FDW ──► DW ──► (green ends)
       (var)   (fixed)        (var)    (fixed)
```

This is worth mimicking, and it costs nothing on the countdown module: every FDW is still the same
fixed length, so it counts down the same number each time. The recycle is invisible to it.

The opposite behaviour is also real - some controllers "rest in WALK" for the whole green and only run
FDW at the end. Either is fine here as long as FDW stays constant.

## SSR relay module (KOOBOOK 4-channel, G3MB-202P type)

One module is currently wired to the three vehicle signal heads (red / yellow / green), leaving one
spare channel. Nameplate specs:

- Input supply: 5 V DC, 160 mA (all four channels)
- Output: 240 VAC 2 A per channel, solid state, normally open, zero-cross
- Board size 57 x 55 x 25 mm, 0.1" header + KF301 screw terminals
- **Low level trigger**: input LOW = relay ON. (The Amazon listing also claims "3.3-5 V high = ON",
  which contradicts the title - confirm with a multimeter on a bare channel before connecting mains.)

The firmware already assumes active-low: `set_traffic_light()` writes `!state` to each pin.

### Load budget

Everything on these heads is LED and tiny compared to the 2 A channel rating:

| Load | Draw @ 120 VAC |
|---|---|
| Vehicle red / yellow / green | well under 100 mA each |
| Ped HAND or MAN | ~62 mA |
| Countdown | ~88 mA (PF ~0.86) |

### Known gotchas

1. **Leakage / ghosting - not an issue in practice so far.** G3MB-202P SSRs carry an internal RC
   snubber that passes a few mA when off, which in theory is enough to make a low-draw LED signal
   module glow faintly. The three vehicle heads on the existing board switch fully on and off with no
   visible ghosting, so this can be ignored unless the ped modules behave differently. If one ever
   does glow when commanded dark, put a bleeder across the *load* (a suitable power resistor, or a
   small incandescent lamp) to give the leakage somewhere to go.
2. **Power the SSR board from the 5 V rail, not from the ESP32's 3.3 V regulator.** 160 mA on the
   3.3 V LDO will brown out the ESP32 mid-cycle.
3. **Low-level trigger suits the 3.3 V ESP32 well** - the GPIO only has to sink to 0 V, so the 3.3 V
   high level never has to clear a 5 V threshold.

## Channel plan

Three vehicle heads use 3 of 4 channels; the ped phase needs 2 (orange + blue), so the second board
from the 2-pack is required. Five outputs, four channels.

### Why it can't be squeezed onto the one board

The tempting trick is to note that WALK and DON'T WALK are mutually exclusive, so a single SPDT relay
could drive both from one channel - NC to orange, NO to blue. That would fit in the spare channel.

**It breaks FDW.** The flashing DON'T WALK phase needs orange chopped at 1 Hz while blue stays dark.
On a complement wiring, flashing orange necessarily flashes blue on in antiphase - the WALK symbol
would strobe through the entire clearance interval, and the countdown module would read the mess on
its blue input as a string of new WALK phases and never count.

Orange has to be independently controllable for the countdown to work at all, so the two hots need two
real channels. The second board is already in hand from the 2-pack, so this costs nothing.

Note the countdown module itself still needs **zero** channels - it is spliced in parallel, so the
total is 5 and not 6.

Power both boards from the 5 V rail, not the ESP32's regulator: 160 mA each, 320 mA total.

| Channel | Signal | Board |
|---|---|---|
| 1-3 | Vehicle red / yellow / green | existing |
| 4 | spare | existing |
| 5 | Ped DON'T WALK (orange) | second board |
| 6 | Ped WALK (blue) | second board |
| 7-8 | spare | second board |

## Suggested GPIO assignment

Currently used: 12 (red), 14 (yellow), 27 (green), 16 (onboard RGB), 25/26 (TF-Luna UART2).

Free and safe for the ped outputs: **GPIO 32 and GPIO 33** - plain outputs, not strapping pins, no
boot-time conflict.

> **GPIO 12 is a strapping pin (MTDI).** Held high at boot it selects 1.8 V flash and the ESP32 will
> not start. A low-level-trigger SSR input is weakly pulled up toward 5 V through its opto, which can
> hold GPIO 12 high during reset. It boots fine today, so this is not urgent.

## TODO

- [ ] Move the vehicle **red** output off GPIO 12 to a non-strapping pin (18, 19, 21, 22 or 23 are
      free). Currently boots fine, but GPIO 12 held high at reset is a latent brick-the-boot risk -
      worth doing on the next wiring pass rather than chasing it later.
- [x] Fix `handleToggleLightMode()` active-low bug - it wrote `LOW` to the three light pins to "turn
      off", which on an active-low board turned all three heads on. Now calls `set_traffic_light(0,0,0)`.
- [x] Ped outputs on GPIO 32 / 33 with the WALK -> FDW -> DW state machine and mutual exclusion.
- [ ] Add the ped controls (WALK length, FDW length, chained on/off, chain phase) to the web settings
      menu. They are settable over `/ped_control` today but have no UI yet.
- [ ] Bench test with the second relay board before connecting mains.

## Mains safety

120 VAC on the relay board. Enclose it, fuse the mains feed, strain-relieve every conductor, bond the
signal housings to ground, and never probe the logic side while the mains side is live.

## Bench test tool

`tools/signal_test_gui.py` (stdlib only, `python tools/signal_test_gui.py`) drives the board over HTTP:

- **Wiring test** - flips `/test_mode` on to suspend the cycle, then asserts individual relay channels
  via `/set_output` so you can confirm which channel lights which wire.
- **Cycle test** - with test mode off, jumps the ped state machine and edits the WALK / FDW durations
  through `/ped_control`.

The countdown digits it draws are a *model* of the real module's learn-then-count behaviour, not a
readback - the module has no data line to report from.
