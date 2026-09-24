#include "proximity.h"
#include "signals.h"
#include "ped.h"
#include "webserver.h"
#include <config.h>

#ifdef DISTANCE_SENSOR_ENABLED
#include <LidarHelper.h>
#endif

static const int MAX_SENSOR_FAILURES = 10;
static const unsigned long SAMPLE_INTERVAL = 500;
static const unsigned long ALERT_FLASH_INTERVAL = 500;
static const int ALERT_FLASH_CYCLES = 3;

static ProximityState state = PROX_DISABLED;
static ProximityZone zone = ZONE_LOST;
static ProximityZone candidateZone = ZONE_LOST;
static int consecutiveReadings = 0;
static int sensorFailCount = 0;

static unsigned long lastSample = 0;
static unsigned long dangerStartTime = 0;
static unsigned long lastFlashToggle = 0;
static int flashCycles = 0;
static bool flashOn = false;

static unsigned long lastPedIndicatorBlink = 0;
static bool pedIndicatorBlinkOn = false;

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

ProximityZone proximityZone() { return zone; }
ProximityState proximityState() { return state; }

void setupProximity()
{
#ifdef DISTANCE_SENSOR_ENABLED
  setupLidar();
#endif
  state = PROX_DISABLED;
  zone = ZONE_LOST;
  candidateZone = ZONE_LOST;
}

static ProximityZone classify(float distance)
{
  if (distance == -1 || distance >= distance_max)
    return ZONE_LOST;
  if (distance > distance_warning)
    return ZONE_CLEAR;
  if (distance > distance_danger)
    return ZONE_WARNING;
  return ZONE_DANGER;
}

// Paint the proximity zone onto the hand/man head. Only ever touches the combo head,
// never the countdown - the countdown is on its own channels precisely so it keeps
// seeing a clean pedestrian cycle while this is flashing for an unrelated reason.
// A real ped phase outranks this; proximity only paints while the phase is at rest.
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

// Read the sensor and settle the zone. Returns true if a fresh reading landed.
static bool sample(unsigned long now)
{
  if (now - lastSample < SAMPLE_INTERVAL)
    return false;
  lastSample = now;

#ifdef DISTANCE_SENSOR_ENABLED
  int16_t distance_cm, strength, temp;
  float distance = -1;

  if (!getOptimalMeasurement(distance_cm, distance, strength, temp))
  {
    sensorFailCount++;
    Serial.printf("Sensor read failed (%d/%d)\n", sensorFailCount, MAX_SENSOR_FAILURES);
    if (sensorFailCount >= MAX_SENSOR_FAILURES)
    {
      Serial.println("Sensor disconnected - disabling for this session");
      distance_sensor_enabled = false;
      ws.textAll("{\"sensor_disconnected\":true}");
    }
    return false;
  }

  sensorFailCount = 0;

  notifyAllClientsDistance(distance, temp);

  ProximityZone reading = classify(distance);

  // Zone persistence: a zone only takes effect after N consecutive agreeing reads.
  // Unlike before, the rest of the loop keeps running while we wait.
  if (reading != candidateZone)
  {
    candidateZone = reading;
    consecutiveReadings = 1;
  }
  else
  {
    consecutiveReadings++;
  }

  if (consecutiveReadings >= zone_persistence && zone != candidateZone)
  {
    zone = candidateZone;
    Serial.printf("[PROX] zone -> %s\n", proximityZoneName(zone));
    ws.textAll("{\"proximity_zone\":\"" + String(proximityZoneName(zone)) + "\"}");

    if (zone == ZONE_DANGER)
      dangerStartTime = now;
    else
      dangerStartTime = 0;
  }
  return true;
#else
  return false;
#endif
}

bool proximityUpdate(unsigned long now)
{
  if (!distance_sensor_enabled)
  {
    state = PROX_DISABLED;
    return true; // cycle freely
  }

  if (state == PROX_DISABLED)
  {
    state = PROX_TRACKING;
    zone = ZONE_LOST;
    candidateZone = ZONE_LOST;
    consecutiveReadings = 0;
  }

  sample(now);

  switch (state)
  {
  case PROX_ALERT:
    if (now - lastFlashToggle >= ALERT_FLASH_INTERVAL)
    {
      lastFlashToggle = now;
      flashOn = !flashOn;
      if (!prox_use_ped)
        set_traffic_light(flashOn, 0, 0);
      else
        set_ped_signal(false, flashOn);

      if (!flashOn && ++flashCycles >= ALERT_FLASH_CYCLES)
        state = PROX_DANGER_CYCLE;
    }
    return false; // alert owns the output

  case PROX_DANGER_CYCLE:
    // Alert has been shown. Let the cycle run again, and re-arm once clear.
    if (zone != ZONE_DANGER)
      state = PROX_TRACKING;
    if (prox_use_ped)
      updatePedIndicator(now);
    return true;

  case PROX_TRACKING:
  default:
    // Entering danger and holding long enough escalates to the alert.
    if (zone == ZONE_DANGER && dangerStartTime > 0 && now - dangerStartTime >= dangerHoldTime)
    {
      state = PROX_ALERT;
      flashCycles = 0;
      flashOn = false;
      lastFlashToggle = now;
      return false;
    }

    if (prox_use_ped)
    {
      // Vehicle light keeps cycling; the ped head carries the warning.
      updatePedIndicator(now);
      return true;
    }

    // Legacy behaviour: proximity paints the vehicle head directly.
    if (zone == ZONE_LOST)
      return true;

    if (zone == ZONE_DANGER)
      set_traffic_light(1, 0, 0);
    else if (zone == ZONE_WARNING)
      set_traffic_light(0, 1, 0);
    else
      set_traffic_light(0, 0, 1);
    return false;
  }
}
