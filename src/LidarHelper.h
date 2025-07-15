#pragma once
#include <Arduino.h>
#include <TFMPlus.h>

#define LIDAR_SERIAL Serial2
#define LIDAR_RX 16
#define LIDAR_TX 17

enum DistanceMode
{
    SHORT_MODE = 0x00,
    LONG_MODE = 0x02
};

extern DistanceMode currentMode;
extern TFMPlus tfm;

void setupLidar();
const char *modeName(DistanceMode m);
void setDistanceMode(DistanceMode m, bool save = true);
void autoSwitchMode(int16_t dist, int16_t str);
bool getOptimalMeasurement(int16_t &outDist,  float &outDist_ft, int16_t &outStr, int16_t &outTemp);