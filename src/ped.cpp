#include "ped.h"
#include "signals.h"
#include <config.h>

#ifdef PED_SIGNAL_ENABLED

const char *pedStateName(PedState state)
{
  switch (state)
  {
  case PED_WALK:
    return "walk";
  case PED_FDW:
    return "fdw";
  case PED_DONT_WALK:
    return "dont_walk";
  default:
    return "off";
  }
}

// Both heads follow the phase together. They are on separate channels only so that
// proximity mode can drive the combo head without the countdown seeing it.
static void driveBothHeads(boolean walk, boolean dont_walk)
{
  set_ped_signal(walk, dont_walk);
  set_countdown_signal(walk, dont_walk);
}

void setPedState(PedState state)
{
  currentPedState = state;
  pedPhaseStart = millis();

  switch (state)
  {
  case PED_WALK:
    driveBothHeads(true, false);
    break;

  case PED_FDW:
    // FDW starts lit, then chops at 1Hz. The flashing is what tells the
    // countdown module to start counting.
    pedBlinkState = true;
    driveBothHeads(false, true);
    break;

  case PED_DONT_WALK:
    driveBothHeads(false, true);
    break;

  case PED_OFF:
    driveBothHeads(false, false);
    break;
  }

  String jsonResponse = "{\"ped_state\":\"" + String(pedStateName(state)) + "\"}";
  ws.textAll(jsonResponse);
}

unsigned long pedPhaseDuration()
{
  return ped_walk_duration + ped_fdw_duration + ped_dw_duration;
}

// A zero-length WALK means the countdown fills the whole phase. Go straight to FDW
// then: passing through PED_WALK for one loop would click the relay and blink the lamp.
static void startPedMovement()
{
  setPedState(ped_walk_effective > 0 ? PED_WALK : PED_FDW);
}

void startPedPhase(unsigned long availableTime)
{
  // FDW is the number the countdown learns, so it is never shortened. What gives is
  // the WALK, and when even a minimum WALK will not fit the movement is skipped.
  unsigned long minWalk = ped_fit_lights ? PED_MIN_WALK_MS : ped_walk_duration;
  if (availableTime < ped_fdw_duration + minWalk)
  {
    Serial.println("Ped phase skipped: vehicle phase (" + String(availableTime) +
                   "ms) too short for FDW (" + String(ped_fdw_duration) +
                   "ms) plus a " + String(minWalk) + "ms WALK");
    setPedState(PED_DONT_WALK);
    return;
  }

  // Rest in WALK: the countdown reaches zero as the vehicle phase ends, so the steady
  // hand falls on the phase that follows - the yellow, when walking on green.
  ped_walk_effective = availableTime - ped_fdw_duration;
  startPedMovement();
}

void updatePedSignal(unsigned long currentMillis)
{
  // The caller's timestamp was taken before cycleLights() ran, and a phase started in
  // there is stamped with a later millis(). Subtracting would wrap to a huge elapsed
  // time and end the new interval on the spot, so take a fresh reading here.
  currentMillis = millis();
  unsigned long elapsed = currentMillis - pedPhaseStart;

  switch (currentPedState)
  {
  case PED_WALK:
    if (elapsed >= ped_walk_effective)
      setPedState(PED_FDW);
    break;

  case PED_FDW:
  {
    // Take the flash from the time since the interval began, not from the last toggle.
    // A slow pass through loop() then delays one edge instead of every edge after it,
    // so the hand cannot drift behind the countdown's own clock.
    bool lit = (elapsed / pedFdwFlashInterval) % 2 == 0;
    if (lit != pedBlinkState)
    {
      pedBlinkState = lit;
      driveBothHeads(false, pedBlinkState);
    }
    if (elapsed >= ped_fdw_duration)
      setPedState(PED_DONT_WALK);
    break;
  }

  case PED_DONT_WALK:
    // Free-running: rest, then recycle. When chained the vehicle cycle starts the
    // next movement instead, so leave the head at rest.
    if (!ped_chained && elapsed >= ped_dw_duration)
      startPedMovement();
    break;

  default:
    break;
  }
}

#endif
