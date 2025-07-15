#include "LidarHelper.h"

#define LIDAR_SERIAL Serial2

const uint32_t DIST_MODE_SWITCH_DELAY_MS = 50;
const uint16_t DIST_THRESHOLD_FT = 15;
const int16_t DIST_STR_THRESHOLD = 1000;

DistanceMode currentMode = SHORT_MODE;
TFMPlus tfm;

void setupLidar()
{
    LIDAR_SERIAL.begin(115200, SERIAL_8N1, LIDAR_RX, LIDAR_TX);
    delay(500);
    tfm.begin(&LIDAR_SERIAL);
    setDistanceMode(SHORT_MODE);
}

const char *modeName(DistanceMode m)
{
    return m == SHORT_MODE ? "Short" : "Long";
}

void setDistanceMode(DistanceMode m, bool save)
{
    byte cmd[] = {0x5A, 0x06, 0x00, 0x00, (byte)m, (byte)(save ? 1 : 0)};
    LIDAR_SERIAL.write(cmd, sizeof(cmd));
    currentMode = m;
}

void autoSwitchMode(int16_t dist, int16_t str)
{
    if ((dist > DIST_THRESHOLD_FT || str < DIST_STR_THRESHOLD) && currentMode != LONG_MODE)
    {
        setDistanceMode(LONG_MODE);
        delay(DIST_MODE_SWITCH_DELAY_MS);
    }
    else if (dist <= DIST_THRESHOLD_FT && str >= DIST_STR_THRESHOLD && currentMode != SHORT_MODE)
    {
        setDistanceMode(SHORT_MODE);
        delay(DIST_MODE_SWITCH_DELAY_MS);
    }
}
bool getOptimalMeasurement(int16_t &outDist,float &outDist_ft, int16_t &outStr, int16_t &outTemp)
{
    int16_t d, s, t;
    if (!tfm.getData(d, s, t))
        return false;
    autoSwitchMode(d, s);
    if (tfm.getData(d, s, t))
    {
        outDist = d;
        outDist_ft = d * 0.0328084; // Convert cm to feet
        outStr = s;
        outTemp = t;
        return true;
    }
    return false;
}