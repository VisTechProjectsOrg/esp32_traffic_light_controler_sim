"""Scenario check for the proximity state machine in src/proximity.cpp.

Reads the tuning constants straight out of the C++ source and replays distance
profiles through a Python mirror of the algorithm, so the thresholds can be
sanity-checked without flashing the board.

Run: python tools/proximity_sim.py
"""

import re, statistics
src = open('src/proximity.cpp', encoding='utf-8').read()
def const(name):
    m = re.search(r'%s\s*=\s*([0-9.]+)f?\s*;' % name, src)
    if not m: raise SystemExit("missing const: "+name)
    return float(m.group(1))

SAMPLE=const('SAMPLE_INTERVAL'); WIN=int(const('MEDIAN_WINDOW')); MINV=int(const('MEDIAN_MIN_VALID'))
MAXR=const('MAX_RANGE_FT'); MARGIN=const('BASELINE_MARGIN_FT'); BSTAB=const('BASELINE_STABLE_FT')
BLEARN=const('BASELINE_LEARN_MS'); ENTRY=const('ENTRY_MAX_GAP_FT')
TOUT=const('TRACK_TIMEOUT_MS'); TMAX=const('TRACK_MAX_MS'); PSTAB=const('PARK_STABLE_FT'); PDWELL=const('PARK_DWELL_MS')
# Settings rather than constants: the Green zone and the approach distance in the web UI.
DIST_MAX = 15.0
APPR = 2.0
print("window=%d @%.0fms (lag %.0fms)  margin=%.1f entry_gap=%.1f approach=%.1f max=%.1f"%(WIN,SAMPLE,WIN*SAMPLE,MARGIN,ENTRY,APPR,DIST_MAX))

class Prox:
    def __init__(s, dist_max=None):
        s.dmax = DIST_MAX if dist_max is None else dist_max
        s.first_guide = None
        s.state='empty'; s.win=[]; s.filt=-1; s.base=-1; s.bvalid=False
        s.cand=-1; s.csince=0; s.t0=0; s.d0=0; s.seen=-1e9; s.pref=0; s.psince=0; s.log=[]
    def step(s, now, dist, strength=400):
        valid = dist is not None and strength>=100 and 0<dist<=MAXR
        s.win.append(dist if valid else None); s.win=s.win[-WIN:]
        good=[v for v in s.win if v is not None]
        s.filt = statistics.median(good) if len(good)>=MINV else -1
        have = s.filt>=0
        entry = min(s.base-MARGIN, s.dmax)
        target = have and s.bvalid and s.filt <= entry
        if target: s.seen=now
        if s.state=='empty':
            obs = s.filt if have else MAXR
            if s.cand<0 or abs(obs-s.cand)>BSTAB: s.cand=obs; s.csince=now
            elif now-s.csince>=BLEARN and (not s.bvalid or abs(s.base-s.cand)>BSTAB):
                s.base=s.cand; s.bvalid=True; s.log.append((now,'baseline learned %.1f ft'%s.base))
        st=s.state
        if st=='empty':
            if target:
                if s.filt < entry-ENTRY:
                    if not s.log or 'REJECT' not in s.log[-1][1]:
                        s.log.append((now,'REJECT appeared mid-range @%.1f'%s.filt))
                else:
                    s.state='tracking'; s.d0=s.filt; s.t0=now; s.log.append((now,'tracking from %.1f'%s.filt))
        elif st=='tracking':
            if not target and now-s.seen>TOUT:
                s.state='empty'; s.log.append((now,'lost before approach confirmed'))
            elif s.d0-s.filt>=APPR:
                s.state='guiding'; s.pref=s.filt; s.psince=now; s.log.append((now,'GUIDING @%.1f ft'%s.filt))
                if s.first_guide is None: s.first_guide = s.filt
            elif now-s.t0>TMAX:
                s.state='empty'; s.bvalid=False; s.cand=-1; s.log.append((now,'static object -> relearn'))
        elif st=='guiding':
            if not target and now-s.seen>TOUT: s.state='empty'; s.log.append((now,'departed'))
            elif abs(s.filt-s.pref)>PSTAB: s.pref=s.filt; s.psince=now
            elif now-s.psince>=PDWELL: s.state='parked'; s.log.append((now,'PARKED @%.1f ft'%s.filt))
        elif st=='parked':
            if not target and now-s.seen>TOUT: s.state='empty'; s.log.append((now,'departed'))

def run(name, profile, secs, expect, dist_max=None):
    p=Prox(dist_max); now=0; guided=False
    while now < secs*1000:
        p.step(now, profile(now/1000.0))
        if p.state in ('guiding','parked'): guided=True
        now += SAMPLE
    # Nothing may ever light up from beyond the max.
    ok = (guided == expect) and (p.first_guide is None or p.first_guide <= p.dmax)
    print("\n%s %-34s guided=%-5s expected=%s" % ("PASS" if ok else "FAIL", name, guided, expect))
    for t,m in p.log[:6]: print("      %6.1fs  %s"%(t/1000,m))
    return ok

results=[]
results.append(run("empty garage (wall @18ft)", lambda t: 18.0, 40, False))
results.append(run("person crossing @8ft", lambda t: 8.0 if 30<=t<32 else 18.0, 40, False))
results.append(run("open door (no return)", lambda t: None, 40, False))
def car(t):
    if t<30: return 18.0
    if t<38: return max(2.0, 18.0-(t-30)*2.0)
    return 2.0
results.append(run("car parking 18->2ft", car, 55, True))

# Garage door open: the beam runs out past the max into the driveway.
results.append(run("door open, person in driveway 19->15.5ft",
                   lambda t: None if t < 30 else max(15.5, 19.0 - (t - 30) * 1.0), 50, False))
results.append(run("door open, car in driveway stops @15.5ft",
                   lambda t: None if t < 30 else max(15.5, 25.0 - (t - 30) * 3.0), 50, False))
def car_open(t):
    if t < 30: return None
    return max(2.0, 25.0 - (t - 30) * 2.0)
results.append(run("door open, car parks 25->2ft", car_open, 55, True))
results.append(run("door open, car parks, max 8ft", car_open, 55, True, dist_max=8.0))
def door_opens(t):
    if t < 30: return 18.0
    if t < 60: return None
    return 17.0 if t < 75 else None   # someone stands at 17ft in the doorway
results.append(run("door opens, someone in doorway", door_opens, 90, False))
print("\n%d/%d scenarios behave as intended" % (sum(results), len(results)))

results.append(run("person walks in from far end",
                   lambda t: 18.0 if t < 30 else max(6.0, 18.0 - (t - 30) * 1.2), 50, True))
print("")
print("Known limitation: a person walking the length of the garage toward the sensor")
print("looks identical to a slow car on distance alone. Signal strength is the")
print("discriminator - watch it in the web diagnostics before hard-coding a gate.")
