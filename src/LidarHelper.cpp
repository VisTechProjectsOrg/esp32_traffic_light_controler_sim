#include "LidarHelper.h"

#define LIDAR_SERIAL Serial2

const uint32_t DIST_MODE_SWITCH_DELAY_MS = 50;
const uint16_t DIST_THRESHOLD_FT = 15;
const int16_t DIST_STR_THRESHOLD = 1000;

DistanceMode currentMode = SHORT_MODE;
TFMPlus tfm;

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

void autoSwitchMode(int16_t dist, int16_t str)
{
    if ((dist > DIST_THRESHOLD_FT || str < DIST_STR_THRESHOLD) &&
        currentMode != LONG_MODE)
    {
        requestDistanceMode(LONG_MODE);
    }
    else if (dist <= DIST_THRESHOLD_FT && str >= DIST_STR_THRESHOLD &&
             currentMode != SHORT_MODE)
    {
        requestDistanceMode(SHORT_MODE);
    }
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
    if (tfm.getData(d, s, t))
    {
        outDist = d;
        outDist_ft = d * 0.0328084f; // cm→ft
        outStr = s;
        outTemp = t;
        return true;
    }
    return false;
}
