#pragma once

#include "state.h"

// Distance sensing.
//
// The core idea is that the sensor sees a *background* - the back wall, a closed
// door, or the void of an open one - and a target only exists when something is
// meaningfully nearer than that. Without a learned baseline every valid distance
// looks like a car, which is why an empty garage used to show a zone forever.
//
// A target also has to earn its way in. Something that appears, sits at a fixed
// distance and leaves is not a car parking; a car closes distance over several
// seconds and then stops. Requiring that approach is what rejects a person walking
// through the beam.
//
// Sampling runs fast and decisions run slow: raw frames at SAMPLE_INTERVAL into a
// median window, so a spike never reaches the state machine in the first place.

enum ProximityZone
{
  ZONE_LOST = -1, // no usable reading
  ZONE_CLEAR = 0,
  ZONE_WARNING = 1,
  ZONE_DANGER = 2
};

enum ProximityState
{
  PROX_DISABLED,  // sensor off or failed out
  PROX_EMPTY,     // nothing nearer than the baseline
  PROX_TRACKING,  // candidate target, approach not yet confirmed
  PROX_GUIDING,   // confirmed approach, zones are live
  PROX_PARKED     // settled, display frozen
};

void setupProximity();

// Returns true when proximity is driving the vehicle lamps this pass. The traffic
// cycle keeps running either way - this only says who owns the output.
bool proximityUpdate(unsigned long now);

ProximityZone proximityZone();
ProximityState proximityState();
const char *proximityZoneName(ProximityZone zone);
const char *proximityStateName(ProximityState state);

// Diagnostics for the web UI.
float proximityRaw();
float proximityFiltered();
int16_t proximityStrength();
float proximityBaseline();
bool proximityBaselineValid();
bool proximitySensorConnected();

// Forget the learned background, e.g. after the garage layout changes.
void proximityRelearnBaseline();
