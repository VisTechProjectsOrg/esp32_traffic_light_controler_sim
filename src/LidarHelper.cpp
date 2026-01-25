#include "LidarHelper.h"

#define LIDAR_SERIAL Serial2

const uint32_t DIST_MODE_SWITCH_DELAY_MS = 50;
const uint16_t DIST_THRESHOLD_FT = 18;      // increased from 15 to reduce mode oscillation
const int16_t DIST_STR_THRESHOLD = 50;
const float MAX_VALID_DISTANCE_FT = 20.0f;  // garage depth - reject readings beyond this
const int FILTER_SIZE = 5;                   // rolling average window

DistanceMode currentMode = SHORT_MODE;
TFMPlus tfm;

// rolling average filter state
static float distanceBuffer[FILTER_SIZE] = {0};
static int bufferIndex = 0;
static int bufferCount = 0;

// — non‑blocking state vars —
static unsigned long lastModeSwitch = 0;
static DistanceMode pendingMode = SHORT_MODE;
static bool modeChangePending = false;

void setupLidar()
{
    LIDAR_SERIAL.begin(115200, SERIAL_8N1, LIDAR_RX, LIDAR_TX);
    tfm.begin(&LIDAR_SERIAL);

    // schedule an initial mode set
    pendingMode = currentMode;
    modeChangePending = true;
    lastModeSwitch = millis();
}

void requestDistanceMode(DistanceMode m, bool save)
{
    // ignore redundant requests
    if ((modeChangePending && pendingMode == m) || currentMode == m)
        return;

    byte cmd[] = {
        0x5A, 0x06, 0x00, 0x00,
        static_cast<byte>(m),
        static_cast<byte>(save ? 1 : 0)};
    LIDAR_SERIAL.write(cmd, sizeof(cmd));

    pendingMode = m;
    modeChangePending = true;
    lastModeSwitch = millis();
}

void processModeSwitch()
{
    if (!modeChangePending)
        return;
    if (millis() - lastModeSwitch >= DIST_MODE_SWITCH_DELAY_MS)
    {
        currentMode = pendingMode;
        modeChangePending = false;
    }
}

void autoSwitchMode(int16_t dist_cm, int16_t str)
{
    float dist_ft = dist_cm * 0.0328084f;

    if ((dist_ft > DIST_THRESHOLD_FT || str < DIST_STR_THRESHOLD) &&
        currentMode != LONG_MODE)
    {
        requestDistanceMode(LONG_MODE);
    }
    else if (dist_ft <= DIST_THRESHOLD_FT && str >= DIST_STR_THRESHOLD &&
             currentMode != SHORT_MODE)
    {
        requestDistanceMode(SHORT_MODE);
    }
}

static float lastFilteredValue = 0;
const float JUMP_THRESHOLD_FT = 1.0f;  // if change > 1ft, reset filter

static void clearFilter()
{
    bufferIndex = 0;
    bufferCount = 0;
    lastFilteredValue = 0;
}

static float addToFilter(float value)
{
    // detect large jump - reset filter for immediate response
    float diff = value - lastFilteredValue;
    if (diff < 0) diff = -diff;
    if (bufferCount > 0 && diff > JUMP_THRESHOLD_FT)
    {
        Serial.printf("[FILTER] Jump detected: %.2f -> %.2f, resetting\n", lastFilteredValue, value);
        clearFilter();
    }

    distanceBuffer[bufferIndex] = value;
    bufferIndex = (bufferIndex + 1) % FILTER_SIZE;
    if (bufferCount < FILTER_SIZE)
        bufferCount++;

    float sum = 0;
    for (int i = 0; i < bufferCount; i++)
        sum += distanceBuffer[i];

    lastFilteredValue = sum / bufferCount;
    return lastFilteredValue;
}

bool getOptimalMeasurement(int16_t &outDist,
                           float &outDist_ft,
                           int16_t &outStr,
                           int16_t &outTemp)
{
    int16_t d, s, t;

    // first sample
    if (!tfm.getData(d, s, t))
        return false;

    // maybe queue a mode change
    autoSwitchMode(d, s);
    processModeSwitch();

    // second sample in the (potentially) new mode
    if (!tfm.getData(d, s, t))
        return false;

    float dist_ft = d * 0.0328084f; // cm→ft

    Serial.printf("[RAW] %d cm = %.2f ft, strength=%d, mode=%s\n",
                  d, dist_ft, s, currentMode == SHORT_MODE ? "SHORT" : "LONG");

    // reject weak signal readings
    if (s < DIST_STR_THRESHOLD)
    {
        Serial.printf("[REJECT] weak signal: %d < %d\n", s, DIST_STR_THRESHOLD);
        clearFilter();
        outDist = -1;
        outDist_ft = -1;
        outStr = s;
        outTemp = t;
        return true;
    }

    // reject readings beyond garage depth
    if (dist_ft > MAX_VALID_DISTANCE_FT)
    {
        Serial.printf("[REJECT] beyond max: %.2f > %.2f ft\n", dist_ft, MAX_VALID_DISTANCE_FT);
        clearFilter();
        outDist = -1;
        outDist_ft = -1;
        outStr = s;
        outTemp = t;
        return true;
    }

    // apply rolling average filter
    float filtered_ft = addToFilter(dist_ft);

    outDist = d;
    outDist_ft = filtered_ft;
    outStr = s;
    outTemp = t;
    return true;
}
