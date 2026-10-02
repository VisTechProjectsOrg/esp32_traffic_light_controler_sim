#pragma once

#include "state.h"

#ifdef PED_SIGNAL_ENABLED

const char *pedStateName(PedState state);

void setPedState(PedState state);

// Start a ped phase, trimming WALK so the whole thing fits inside the given vehicle
// phase. FDW is never trimmed - a short clearance interval would teach the countdown
// module the wrong number - so if even FDW alone does not fit, the phase is skipped.
void startPedPhase(unsigned long availableTime);

void updatePedSignal(unsigned long currentMillis);

// Wall time one complete pedestrian movement needs: WALK + FDW + the steady rest.
// When chained, this is the floor the vehicle phase has to clear.
unsigned long pedPhaseDuration();

// Shortest WALK required when the crossing is fitted into the light timings. Zero: the
// settings form caps the countdown at the light's length and WALK gets what is left,
// so the only movement still skipped is one whose light is shorter than the FDW.
const unsigned long PED_MIN_WALK_MS = 0;

#endif
