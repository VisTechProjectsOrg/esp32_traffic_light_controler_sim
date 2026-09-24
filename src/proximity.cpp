#include "proximity.h"
#include "signals.h"
#include "ped.h"
#include "webserver.h"
#include <config.h>

#ifdef DISTANCE_SENSOR_ENABLED
#include <LidarHelper.h>
#endif

// --- tuning ---------------------------------------------------------------

static const unsigned long SAMPLE_INTERVAL = 50; // 20Hz. The TF-Luna will do far
                                                 // more; 2Hz gave no statistics at all.
static const int MEDIAN_WINDOW = 9;
static const int MEDIAN_MIN_VALID = 5; // need this many good samples in the window

static const float MAX_RANGE_FT = 20.0f; // garage depth

// A target must be this much nearer than the background to count at all.
static const float BASELINE_MARGIN_FT = 2.0f;
// Background is considered settled when it holds this steady for BASELINE_LEARN_MS.
static const float BASELINE_STABLE_FT = 0.5f;
static const unsigned long BASELINE_LEARN_MS = 20000;

// A car closes this much distance on its way in. A person crossing does not.
static const float APPROACH_MIN_FT = 2.0f;
// A car enters through the far boundary, so its first detection lands just inside
// the baseline. Someone stepping into the beam appears mid-range out of nowhere.
// Rejecting that discontinuity is the cheapest filter for a crossing pedestrian.
// Sized for the median window's lag at a realistic entry speed.
static const float ENTRY_MAX_GAP_FT = 5.0f;
static const unsigned long TRACK_TIMEOUT_MS = 2000;  // target gone this long -> EMPTY
static const unsigned long TRACK_MAX_MS = 20000;     // static object, give up and relearn

static const float PARK_STABLE_FT = 0.33f; // ~4 inches
static const unsigned long PARK_DWELL_MS = 3000;

static const unsigned long ALERT_FLASH_INTERVAL = 500;
static const int ALERT_FLASH_CYCLES = 3;
static const unsigned long DIAG_PUSH_INTERVAL = 500; // don't spam the socket at 20Hz

// --- state ----------------------------------------------------------------

static ProximityState state = PROX_DISABLED;
static ProximityZone zone = ZONE_LOST;

static float window[MEDIAN_WINDOW];
static bool windowValid[MEDIAN_WINDOW];
static int windowIndex = 0;
static int windowFill = 0;

static float rawFt = -1;
static float filteredFt = -1;
static int16_t lastStrength = 0;
static int16_t lastTemp = 0;

static float baseline = -1;
static bool baselineValid = false;
static float baselineCandidate = -1;
static unsigned long baselineStableSince = 0;

static unsigned long lastSample = 0;
static unsigned long lastDiagPush = 0;

static float trackStartDistance = 0;
static unsigned long trackStartTime = 0;
static unsigned long lastTargetSeen = 0;

static float parkReference = 0;
static unsigned long parkStableSince = 0;

static unsigned long lastFlashToggle = 0;
static int flashCycles = 0;
static bool flashOn = false;
static bool alertShown = false;

static unsigned long lastPedIndicatorBlink = 0;
static bool pedIndicatorBlinkOn = false;

// --- helpers --------------------------------------------------------------

const char *proximityZoneName(ProximityZone z)
{
  switch (z)
  {
  case ZONE_CLEAR:
    return "clear";
  case ZONE_WARNING:
    return "warning";
  case ZONE_DANGER:
    return "danger";
  default:
    return "lost";
  }
}

const char *proximityStateName(ProximityState s)
{
  switch (s)
  {
  case PROX_EMPTY:
    return "empty";
  case PROX_TRACKING:
    return "tracking";
  case PROX_GUIDING:
    return "guiding";
  case PROX_PARKED:
    return "parked";
  default:
    return "disabled";
  }
}

ProximityZone proximityZone() { return zone; }
ProximityState proximityState() { return state; }
float proximityRaw() { return rawFt; }
float proximityFiltered() { return filteredFt; }
int16_t proximityStrength() { return lastStrength; }
float proximityBaseline() { return baseline; }
bool proximityBaselineValid() { return baselineValid; }

void proximityRelearnBaseline()
{
  baselineValid = false;
  baseline = -1;
  baselineCandidate = -1;
  baselineStableSince = 0;
  Serial.println("[PROX] baseline cleared, relearning");
}

static void resetWindow()
{
  windowIndex = 0;
  windowFill = 0;
  for (int i = 0; i < MEDIAN_WINDOW; i++)
    windowValid[i] = false;
  filteredFt = -1;
}

void setupProximity()
{
#ifdef DISTANCE_SENSOR_ENABLED
  setupLidar();
#endif
  state = PROX_DISABLED;
  zone = ZONE_LOST;
  resetWindow();
  proximityRelearnBaseline();
}

// Median of the valid entries. Unlike a rolling mean this needs no "large jump"
// escape hatch - and that escape hatch was the bug, because it cleared the filter
// exactly when a transient appeared and let it through at full weight.
static bool medianOfWindow(float &out)
{
  float vals[MEDIAN_WINDOW];
  int n = 0;
  for (int i = 0; i < MEDIAN_WINDOW; i++)
    if (windowValid[i])
      vals[n++] = window[i];

  if (n < MEDIAN_MIN_VALID)
    return false;

  for (int i = 1; i < n; i++)
  {
    float key = vals[i];
    int j = i - 1;
    while (j >= 0 && vals[j] > key)
    {
      vals[j + 1] = vals[j];
      j--;
    }
    vals[j + 1] = key;
  }

  out = vals[n / 2];
  return true;
}

static void pushSample(bool valid, float ft)
{
  window[windowIndex] = ft;
  windowValid[windowIndex] = valid;
  windowIndex = (windowIndex + 1) % MEDIAN_WINDOW;
  if (windowFill < MEDIAN_WINDOW)
    windowFill++;
}

// Track the background while nothing is happening. Holding steady for long enough
// promotes it to the baseline. A run with no usable return at all - open door, or
// nothing within range - settles to MAX_RANGE_FT, which is just as valid a
// background as a wall.
static void updateBaseline(unsigned long now, bool haveReading)
{
  if (state != PROX_EMPTY && state != PROX_DISABLED)
    return;

  float observed = haveReading ? filteredFt : MAX_RANGE_FT;

  if (baselineCandidate < 0 || fabsf(observed - baselineCandidate) > BASELINE_STABLE_FT)
  {
    baselineCandidate = observed;
    baselineStableSince = now;
    return;
  }

  if (now - baselineStableSince >= BASELINE_LEARN_MS)
  {
    if (!baselineValid || fabsf(baseline - baselineCandidate) > BASELINE_STABLE_FT)
    {
      baseline = baselineCandidate;
      baselineValid = true;
      Serial.printf("[PROX] baseline learned: %.2f ft\n", baseline);
      ws.textAll("{\"proximity_baseline\":" + String(baseline, 2) + "}");
    }
  }
}

static bool targetPresent()
{
  if (filteredFt < 0)
    return false;
  if (!baselineValid)
    return false; // refuse to guess before the background is known
  return filteredFt < (baseline - BASELINE_MARGIN_FT);
}

static ProximityZone classify(float distance)
{
  if (distance < 0)
    return ZONE_LOST;
  if (distance > distance_warning)
    return ZONE_CLEAR;
  if (distance > distance_danger)
    return ZONE_WARNING;
  return ZONE_DANGER;
}

// Paint the zone onto the hand/man head. Only ever touches the combo head, never
// the countdown - the countdown has its own channels precisely so it keeps seeing a
// clean pedestrian cycle while this flashes for an unrelated reason. A real ped
// phase outranks this; proximity only paints while the phase is at rest.
static void updatePedIndicator(unsigned long now)
{
#ifdef PED_SIGNAL_ENABLED
  if (currentPedState == PED_WALK || currentPedState == PED_FDW)
    return;

  switch (zone)
  {
  case ZONE_CLEAR:
    set_ped_signal(true, false); // man
    break;

  case ZONE_WARNING:
    if (now - lastPedIndicatorBlink >= ALERT_FLASH_INTERVAL)
    {
      lastPedIndicatorBlink = now;
      pedIndicatorBlinkOn = !pedIndicatorBlinkOn;
      set_ped_signal(false, pedIndicatorBlinkOn); // flashing hand
    }
    break;

  case ZONE_DANGER:
    set_ped_signal(false, true); // solid hand
    break;

  default:
    set_ped_signal(false, true); // at rest, hand
    break;
  }
#endif
}

static void pushDiagnostics(unsigned long now)
{
  if (now - lastDiagPush < DIAG_PUSH_INTERVAL)
    return;
  lastDiagPush = now;

  String msg = "{\"proximity\":{\"state\":\"" + String(proximityStateName(state)) +
               "\",\"zone\":\"" + String(proximityZoneName(zone)) +
               "\",\"raw\":" + String(rawFt, 2) +
               ",\"filtered\":" + String(filteredFt, 2) +
               ",\"strength\":" + String(lastStrength) +
               ",\"baseline\":" + String(baseline, 2) +
               ",\"baseline_valid\":" + String(baselineValid ? "true" : "false") + "}}";
  ws.textAll(msg);

  notifyAllClientsDistance(filteredFt, lastTemp);
}

// --- sampling -------------------------------------------------------------

static bool sample(unsigned long now)
{
  if (now - lastSample < SAMPLE_INTERVAL)
    return false;
  lastSample = now;

#ifdef DISTANCE_SENSOR_ENABLED
  int16_t d_cm, strength, temp;
  float ft;

  if (!readRawSample(d_cm, ft, strength, temp))
  {
    pushSample(false, 0);
    return true;
  }

  lastStrength = strength;
  lastTemp = temp;
  rawFt = ft;

  // Strength is the cheapest discriminator there is and the old code read it and
  // threw it away. Under spec minimum means the return is not trustworthy;
  // saturated means the receiver is blinded.
  bool valid = strength >= LIDAR_STRENGTH_MIN &&
               (uint16_t)strength != LIDAR_STRENGTH_SATURATED &&
               ft > 0 && ft <= MAX_RANGE_FT;

  pushSample(valid, ft);

  float med;
  filteredFt = medianOfWindow(med) ? med : -1;
  return true;
#else
  return false;
#endif
}

// --- state machine --------------------------------------------------------

bool proximityUpdate(unsigned long now)
{
  if (!distance_sensor_enabled)
  {
    if (state != PROX_DISABLED)
    {
      state = PROX_DISABLED;
      zone = ZONE_LOST;
      resetWindow();
    }
    return true; // cycle freely
  }

  if (state == PROX_DISABLED)
  {
    state = PROX_EMPTY;
    zone = ZONE_LOST;
    resetWindow();
  }

  sample(now);

  bool haveReading = filteredFt >= 0;
  bool target = targetPresent();
  if (target)
    lastTargetSeen = now;

  updateBaseline(now, haveReading);

  switch (state)
  {
  case PROX_EMPTY:
    zone = ZONE_LOST;
    if (target)
    {
      if (filteredFt < baseline - BASELINE_MARGIN_FT - ENTRY_MAX_GAP_FT)
      {
        // Materialised mid-range rather than entering from the far end.
        Serial.printf("[PROX] ignoring target that appeared at %.2f ft (baseline %.2f)\n",
                      filteredFt, baseline);
        break;
      }
      state = PROX_TRACKING;
      trackStartDistance = filteredFt;
      trackStartTime = now;
      alertShown = false;
      Serial.printf("[PROX] tracking from %.2f ft\n", trackStartDistance);
    }
    break;

  case PROX_TRACKING:
    zone = ZONE_LOST; // nothing shown until the approach is confirmed
    if (!target && now - lastTargetSeen > TRACK_TIMEOUT_MS)
    {
      // Appeared, sat there, left. Not a car parking - this is the crossing
      // pedestrian, and it never reaches the lights.
      state = PROX_EMPTY;
      Serial.println("[PROX] target lost before approach confirmed");
      break;
    }
    if (trackStartDistance - filteredFt >= APPROACH_MIN_FT)
    {
      state = PROX_GUIDING;
      parkReference = filteredFt;
      parkStableSince = now;
      Serial.println("[PROX] approach confirmed, guiding");
    }
    else if (now - trackStartTime > TRACK_MAX_MS)
    {
      // Something is simply parked in the beam. Treat it as the new background.
      state = PROX_EMPTY;
      proximityRelearnBaseline();
      Serial.println("[PROX] static object, relearning baseline");
    }
    break;

  case PROX_GUIDING:
    if (!target && now - lastTargetSeen > TRACK_TIMEOUT_MS)
    {
      state = PROX_EMPTY;
      zone = ZONE_LOST;
      break;
    }

    zone = classify(filteredFt);

    if (fabsf(filteredFt - parkReference) > PARK_STABLE_FT)
    {
      parkReference = filteredFt;
      parkStableSince = now;
    }
    else if (now - parkStableSince >= PARK_DWELL_MS)
    {
      state = PROX_PARKED;
      Serial.printf("[PROX] parked at %.2f ft\n", filteredFt);
    }
    break;

  case PROX_PARKED:
    if (!target && now - lastTargetSeen > TRACK_TIMEOUT_MS)
    {
      state = PROX_EMPTY;
      zone = ZONE_LOST;
      Serial.println("[PROX] vehicle departed");
    }
    break;

  default:
    break;
  }

  pushDiagnostics(now);

  // --- output ---

  if (prox_use_ped)
  {
    // Vehicle light keeps cycling; the hand/man head carries the warning.
    if (state == PROX_GUIDING)
      updatePedIndicator(now);
    else if (state == PROX_PARKED)
      set_ped_signal(false, true); // settled, steady hand
    return true;
  }

  // Legacy behaviour: proximity paints the vehicle head directly.
  if (state != PROX_GUIDING)
    return true;

  // First entry into danger gets the attention flash, once per approach.
  if (zone == ZONE_DANGER && !alertShown)
  {
    if (flashCycles == 0 && lastFlashToggle == 0)
      lastFlashToggle = now;

    if (now - lastFlashToggle >= ALERT_FLASH_INTERVAL)
    {
      lastFlashToggle = now;
      flashOn = !flashOn;
      set_traffic_light(flashOn, 0, 0);
      if (!flashOn && ++flashCycles >= ALERT_FLASH_CYCLES)
      {
        alertShown = true;
        flashCycles = 0;
        lastFlashToggle = 0;
      }
    }
    return false;
  }

  if (zone == ZONE_DANGER)
    set_traffic_light(1, 0, 0);
  else if (zone == ZONE_WARNING)
    set_traffic_light(0, 1, 0);
  else if (zone == ZONE_CLEAR)
    set_traffic_light(0, 0, 1);
  return false;
}
