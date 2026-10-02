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

#endif
