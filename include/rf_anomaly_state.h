#pragma once

#include <Arduino.h>
#include <map>

/**
 * RF Anomaly State - Per-vehicle RF anomaly detection tracking
 */
struct RFState {
    String state = "NORMAL";        // NORMAL, WARNING, CRITICAL
    float score = 0.0f;             // Distance to k-th neighbor
    float confidence = 0.0f;        // Voting confidence %
    uint32_t lastPublish = 0;       // Last anomaly publish time
};

extern std::map<String, RFState> rfState;
