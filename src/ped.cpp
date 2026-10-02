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
    pedBlinkPrevious = pedPhaseStart;
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

void startPedPhase(unsigned long availableTime)
{
  if (availableTime < ped_fdw_duration)
  {
    Serial.println("Ped phase skipped: vehicle phase (" + String(availableTime) +
                   "ms) shorter than FDW (" + String(ped_fdw_duration) + "ms)");
    setPedState(PED_DONT_WALK);
    return;
  }

  unsigned long room = availableTime - ped_fdw_duration;
  ped_walk_effective = ped_walk_duration;
  if (ped_walk_effective > room)
  {
    ped_walk_effective = room;
    Serial.println("Ped WALK trimmed to " + String(room) + "ms to fit the vehicle phase");
  }

  setPedState(PED_WALK);
}

void updatePedSignal(unsigned long currentMillis)
{
  unsigned long elapsed = currentMillis - pedPhaseStart;

  switch (currentPedState)
  {
  case PED_WALK:
    if (elapsed >= ped_walk_effective)
      setPedState(PED_FDW);
    break;

  case PED_FDW:
    if (currentMillis - pedBlinkPrevious >= pedFdwFlashInterval)
    {
      pedBlinkPrevious = currentMillis;
      pedBlinkState = !pedBlinkState;
      driveBothHeads(false, pedBlinkState);
    }
    if (elapsed >= ped_fdw_duration)
      setPedState(PED_DONT_WALK);
    break;

  case PED_DONT_WALK:
    // Free-running: rest, then recycle. When chained the vehicle cycle starts the
    // next movement instead, so leave the head at rest.
    if (!ped_chained && elapsed >= ped_dw_duration)
      setPedState(PED_WALK);
    break;

  default:
    break;
  }
}

#endif
