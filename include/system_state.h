#pragma once

#include <Arduino.h>

/**
 * System State - Centralized timer and LED management
 */
struct SystemState {
    uint32_t lastWiFiCheckTime = 0;
    uint32_t lastNodeStatusTime = 0;
    uint32_t lastGatewayGpsStatusTime = 0;
    uint32_t lastBlinkTime = 0;
    bool ledOn = false;
};

extern SystemState sys;
