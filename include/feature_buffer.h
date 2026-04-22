#pragma once

#include <Arduino.h>
#include <map>
#include <vector>

/**
 * Feature Sample Buffer - Sliding window of RF samples per vehicle
 */
struct FeatureSample {
    uint32_t received_at_ms;
    int16_t rssi;
    int8_t snr;
    uint32_t packet_ts;
};

extern std::map<String, std::vector<FeatureSample>> featureSamples;

/**
 * Cleanup old samples outside window
 */
void cleanupFeatureSamples(const String& vehicle_id, uint32_t window_ms);
