#pragma once
#include <Arduino.h>
#include <TFMPlus.h>

#define LIDAR_SERIAL Serial2
#define LIDAR_RX 25
#define LIDAR_TX 26

// TF-Luna returns 65535 when the receiver is saturated, and its own spec calls
// anything under 100 unreliable. Both are judged in proximity.cpp, not here -
// this file is just the driver.
static const int16_t LIDAR_STRENGTH_MIN = 100;
static const uint16_t LIDAR_STRENGTH_SATURATED = 65535;

enum DistanceMode
{
    SHORT_MODE = 0x00,
    LONG_MODE = 0x02
};

extern DistanceMode currentMode;
extern TFMPlus tfm;

void setupLidar();

void requestDistanceMode(DistanceMode m, bool save = true);
void processModeSwitch();
void autoSwitchMode(int16_t dist, int16_t str);

// One raw frame, no filtering and no validity policy. Returns false only when the
// sensor gave us nothing at all. Filtering, strength gating and range limits all
// live in proximity.cpp so there is a single place that decides what is real.
bool readRawSample(int16_t &outDist_cm, float &outDist_ft, int16_t &outStr, int16_t &outTemp);
