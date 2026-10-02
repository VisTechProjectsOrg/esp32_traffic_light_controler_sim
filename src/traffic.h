#pragma once

#include "state.h"

void randomBlink();

// The phase machine always advances, every loop pass. Whether it is allowed to
// write the lamps is a separate question, so that proximity can take the output
// for a while without the cycle losing time. On release the lamps are repainted
// to wherever the cycle has got to in the meantime.
void cycleLights();
void setTrafficOutputOwner(bool proximityOwnsLamps);
