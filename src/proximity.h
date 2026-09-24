#pragma once

#include "state.h"

// Distance sensing, as an explicit state machine rather than a pile of booleans.
//
// The old version encoded this in four overlapping flags (cycling, hasFlashedInDanger,
// inDangerCycleMode, dangerFlashing) and bailed out of loop() with a bare return while
// waiting for zone persistence, which froze the traffic cycle for a second and a half
// on every zone change. Nothing here returns early; proximityUpdate() runs every pass
// and reports back whether the traffic cycle is allowed to run.

enum ProximityZone
{
  ZONE_LOST = -1, // out of range or beyond distance_max
  ZONE_CLEAR = 0,
  ZONE_WARNING = 1,
  ZONE_DANGER = 2
};

enum ProximityState
{
  PROX_DISABLED,     // sensor off or failed out
  PROX_TRACKING,     // following the zone
  PROX_ALERT,        // flashing the alert, three cycles
  PROX_DANGER_CYCLE  // alert done, normal cycle resumes while still in danger
};

void setupProximity();

// Returns true when the traffic cycle may run this pass.
bool proximityUpdate(unsigned long now);

ProximityZone proximityZone();
ProximityState proximityState();
const char *proximityZoneName(ProximityZone zone);
