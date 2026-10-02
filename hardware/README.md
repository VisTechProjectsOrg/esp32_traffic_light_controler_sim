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

The countdown reads the WALK and DON'T WALK hots. It can be spliced in parallel with the combo module
and needs no channel of its own - but **this build gives it its own pair of channels** instead. See
*Why it gets its own channels* below.

### How the countdown reads the phase

Both modules have the identical three pigtails (white / blue / orange). Spliced colour to colour, the
whole ped head - symbols and countdown - would run on two switched channels total:

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

### Why it gets its own channels

Proximity mode uses the hand/man head as its indicator so the vehicle light can keep cycling
undisturbed - flashing hand for the warning zone, solid hand for danger. On a parallel splice the
countdown would read that warning flash as a clearance interval, count nonsense, and **relearn from
it**, corrupting the real pedestrian cycle's number too. It cannot tell the two apart; they are
electrically identical.

So the countdown gets its own pair of channels (GPIO 18 / 19) and is driven only by the real ped
phase. `set_ped_signal()` and `set_countdown_signal()` in `src/signals.cpp` are separate for exactly
this reason: `ped.cpp` drives both together, `proximity.cpp` only ever touches the combo head.

Total is then 7 channels, not 5.

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

The vehicle head uses 3 of the 4 channels on the first board; the ped phase needs 2 and the countdown
another 2, so the second board from the 2-pack is required. Seven outputs, eight channels.

### Why it can't be squeezed onto the one board

The tempting trick is to note that WALK and DON'T WALK are mutually exclusive, so a single SPDT relay
could drive both from one channel - NC to orange, NO to blue. That would fit in the spare channel.

**It breaks FDW.** The flashing DON'T WALK phase needs orange chopped at 1 Hz while blue stays dark.
On a complement wiring, flashing orange necessarily flashes blue on in antiphase - the WALK symbol
would strobe through the entire clearance interval, and the countdown module would read the mess on
its blue input as a string of new WALK phases and never count.

Orange has to be independently controllable for the countdown to work at all, so the two hots need two
real channels. The second board is already in hand from the 2-pack, so this costs nothing.

The countdown could have needed zero channels by being spliced in parallel, but this build gives it
its own pair - see *Why it gets its own channels* above. Total is 7.

Power both boards from the 5 V rail, not the ESP32's regulator: 160 mA each, 320 mA total.

| Channel | Signal | GPIO | Board |
|---|---|---|---|
| 1 | Vehicle red | 21 | existing |
| 2 | Vehicle yellow | 22 | existing |
| 3 | Vehicle green | 23 | existing |
| 4 | spare | - | existing |
| 5 | Ped DON'T WALK (orange) | 32 | second |
| 6 | Ped WALK (blue) | 33 | second |
| 7 | Countdown DON'T WALK (orange) | 18 | second |
| 8 | Countdown WALK (blue) | 19 | second |

## Suggested GPIO assignment

Currently used: 12 (red), 14 (yellow), 27 (green), 16 (onboard RGB), 25/26 (TF-Luna UART2).

Free and safe for the ped outputs: **GPIO 32 and GPIO 33** - plain outputs, not strapping pins, no
boot-time conflict.

> **The whole pin map was reassigned** during the rewire. Vehicle lamps moved to GPIO 21/22/23 so the
> ribbon to board 1 runs in channel order, and vehicle red specifically had to leave GPIO 12: that is a
> strapping pin (MTDI), and held high at reset the ESP32 selects 1.8 V flash and will not boot. A
> low-level-trigger SSR input is weakly pulled toward 5 V through its opto, so it had been booting on
> luck rather than design. 16 (onboard RGB) and 25/26 (TF-Luna UART) stay clear.

## TODO

- [x] Move the vehicle **red** output off GPIO 12 - now on GPIO 21.
- [x] Fix `handleToggleLightMode()` active-low bug - it wrote `LOW` to the three light pins to "turn
      off", which on an active-low board turned all three heads on. Now calls `set_traffic_light(0,0,0)`.
- [x] Ped outputs on GPIO 32 / 33 with the WALK -> FDW -> DW state machine and mutual exclusion.
- [ ] Add the ped controls (WALK length, FDW length, chained on/off, chain phase, and the
      proximity-on-ped-head toggle) to the web settings menu. Settable over `/ped_control` today but
      no UI yet.
- [ ] Bench test with the second relay board before connecting mains.
- [ ] Green glare: try neutral density film behind the lens. Firmware burst-fire dimming was
      considered and dropped - see *Dimming the green*.
- [ ] `src/config.h` is listed in `.gitignore` but is actually tracked, so the WiFi credentials are in
      git history. Untrack it, add a `config.example.h`, and rotate the password.

## Bench wiring

### Conductor count

Neutral and ground are commoned across everything, so each head carries one hot per lamp plus a
shared return. **Both** ped modules are 3-wire, which is the ITE PTCSI standard for pedestrian
modules: orange (hand), blue (walking person), white (common). Confirmed against the spec and against
`photos/20260923_223046.jpg`, where the factory wire nut parallels the two modules' blues.

| Head | Hots | Conductors (+ ground) |
|---|---|---|
| Vehicle R/Y/G | 3 | 4 |
| Ped combo (hand/man) | 2 | 3 |
| Countdown | 2 | 3 |

Seven switched hots in total, across the two 4-channel boards.

Module pigtails are 18 AWG, so stranded ends terminate in **red** forks (22-16 AWG). Use a ratcheting
crimper - a pliers-crimped fork on mains is a real failure point.

### Bus strips

Three Glarks barrier strips, each combed into a single node. A dual-row strip ties each position's
left and right screw internally, so combing one row makes every screw on that strip one node.

| Strip | Node | Lands |
|---|---|---|
| A | Mains hot | incoming hot + the 7 SSR channel inputs |
| B | Neutral | incoming neutral + 5 module neutrals (3 vehicle, combo, countdown) |
| C | Ground | incoming ground + both signal housings |

SSR channel *outputs* run point to point to their module hot - they do not bus.

### Order of work

Mains disconnected for steps 1-5.

1. Mount the three strips near the SSR boards, keeping the hot strip away from the 5 V logic wiring.
2. **Ring out every pigtail with the heads unpowered.** The ITE colours below match these modules, but
   installer conventions vary and this is the one step firmware cannot undo. Record what you find.
3. Land neutrals and grounds: five whites to strip B, both housing bonds to strip C.
4. Land hots: incoming hot to strip A, then a jumper from strip A to each of the 7 channel inputs.
5. Land channel outputs to their module hots per the channel plan.
6. **Confirm LOW = ON** on a bare channel with a meter before mains. The board is low-level trigger and
   the product listing contradicts itself on this.
7. Fuse the incoming hot and strain-relieve every conductor entering the housing.

### Then test before trusting it

`python tools/signal_test_gui.py`, host set to the ESP32's IP.

- Tick **Test mode** first - it suspends the cycle so it cannot stomp a manual assertion.
- Assert each channel one at a time and confirm which lamp lights. This is what catches a swapped
  orange/blue before the ped phase ever runs.
- Untick test mode, then use the cycle panel to run WALK -> FDW -> DW.

The countdown stays **blank for its first cycle** - that is the self-learning pass, not a fault.

## Dimming the green

The green module sits at eye level and is glaring. An SCR phase dimmer (Gebildet 2000W type) is on
hand, but it is very unlikely to work here:

- **Minimum load.** SCR phase dimmers need 60-100 W to latch reliably. The green module is 7.5 W.
  Below holding current the SCR conducts erratically - flicker and buzz, not dimming.
- **The module is regulated.** The 80-135 VAC nameplate means a wide-input switch-mode constant-current
  driver, which holds LED current flat across that entire range. Reducing RMS voltage changes nothing
  until the driver drops out.
- **Phase-chopped AC into an SMPS input** stresses the rectifier and bulk cap, on a 2016 ETL-listed
  module that is not easily replaced.

Bench test before committing: fused cord -> dimmer -> one green module, nothing else. Sweep the knob
and record whether output changes at all, and whether there is flicker or audible buzz. **Stop on buzz
or stutter** - that is the SCR failing to latch and it is hard on the driver.

### Not doing it in firmware

Burst firing - skipping whole mains half-cycles through the zero-cross SSR - was considered and
dropped. For the record, so it does not get re-proposed:

- The likeliest outcome is **no dimming at all**. These drivers are specced to ride out brownouts, so
  the bulk cap holds LED current flat through a skipped half-cycle: all of the driver stress, none of
  the benefit.
- Zero-cross switching at 60 Hz gives a 2-3 position brightness switch, not continuous dimming.
  Envelope frequency is `120/den` Hz, so only 1/2 is even a candidate; below 50% is a visible strobe.
- The repeated inrush into the electrolytic shortens driver life with no symptom until it fails. Not
  a trade worth making for a 7.5 W lamp.

**The fix is optical.** Neutral density or window tint film behind the green lens: no electrical risk,
reversible, tunable by layering, and it works regardless of what the driver does. One layer is roughly
half the output, two layers roughly a quarter.

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
