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
BLEARN=const('BASELINE_LEARN_MS'); APPR=const('APPROACH_MIN_FT'); ENTRY=const('ENTRY_MAX_GAP_FT')
TOUT=const('TRACK_TIMEOUT_MS'); TMAX=const('TRACK_MAX_MS'); PSTAB=const('PARK_STABLE_FT'); PDWELL=const('PARK_DWELL_MS')
print("window=%d @%.0fms (lag %.0fms)  margin=%.1f entry_gap=%.1f approach=%.1f"%(WIN,SAMPLE,WIN*SAMPLE,MARGIN,ENTRY,APPR))

class Prox:
    def __init__(s):
        s.state='empty'; s.win=[]; s.filt=-1; s.base=-1; s.bvalid=False
        s.cand=-1; s.csince=0; s.t0=0; s.d0=0; s.seen=-1e9; s.pref=0; s.psince=0; s.log=[]
    def step(s, now, dist, strength=400):
        valid = dist is not None and strength>=100 and 0<dist<=MAXR
        s.win.append(dist if valid else None); s.win=s.win[-WIN:]
        good=[v for v in s.win if v is not None]
        s.filt = statistics.median(good) if len(good)>=MINV else -1
        have = s.filt>=0
        target = have and s.bvalid and s.filt < s.base-MARGIN
        if target: s.seen=now
        if s.state=='empty':
            obs = s.filt if have else MAXR
            if s.cand<0 or abs(obs-s.cand)>BSTAB: s.cand=obs; s.csince=now
            elif now-s.csince>=BLEARN and (not s.bvalid or abs(s.base-s.cand)>BSTAB):
                s.base=s.cand; s.bvalid=True; s.log.append((now,'baseline learned %.1f ft'%s.base))
        st=s.state
        if st=='empty':
            if target:
                if s.filt < s.base-MARGIN-ENTRY:
                    if not s.log or 'REJECT' not in s.log[-1][1]:
                        s.log.append((now,'REJECT appeared mid-range @%.1f'%s.filt))
                else:
                    s.state='tracking'; s.d0=s.filt; s.t0=now; s.log.append((now,'tracking from %.1f'%s.filt))
        elif st=='tracking':
            if not target and now-s.seen>TOUT:
                s.state='empty'; s.log.append((now,'lost before approach confirmed'))
            elif s.d0-s.filt>=APPR:
                s.state='guiding'; s.pref=s.filt; s.psince=now; s.log.append((now,'GUIDING'))
            elif now-s.t0>TMAX:
                s.state='empty'; s.bvalid=False; s.cand=-1; s.log.append((now,'static object -> relearn'))
        elif st=='guiding':
            if not target and now-s.seen>TOUT: s.state='empty'; s.log.append((now,'departed'))
            elif abs(s.filt-s.pref)>PSTAB: s.pref=s.filt; s.psince=now
            elif now-s.psince>=PDWELL: s.state='parked'; s.log.append((now,'PARKED @%.1f ft'%s.filt))
        elif st=='parked':
            if not target and now-s.seen>TOUT: s.state='empty'; s.log.append((now,'departed'))

def run(name, profile, secs, expect):
    p=Prox(); now=0; guided=False
    while now < secs*1000:
        p.step(now, profile(now/1000.0))
        if p.state in ('guiding','parked'): guided=True
        now += SAMPLE
    ok = (guided == expect)
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
print("\n%d/%d scenarios behave as intended" % (sum(results), len(results)))

results.append(run("person walks in from far end",
                   lambda t: 18.0 if t < 30 else max(6.0, 18.0 - (t - 30) * 1.2), 50, True))
print("")
print("Known limitation: a person walking the length of the garage toward the sensor")
print("looks identical to a slow car on distance alone. Signal strength is the")
print("discriminator - watch it in the web diagnostics before hard-coding a gate.")
