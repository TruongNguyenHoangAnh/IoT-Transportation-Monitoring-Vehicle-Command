#ifndef RF_ANOMALY_HANDLER_H
#define RF_ANOMALY_HANDLER_H

#include <Arduino.h>
#include <string>

/**
 * Handle RF anomaly detection and publishing
 * @param vehicle_id Vehicle identifier
 * @param rssi Signal strength (RSSI)
 * @param snr Signal-to-noise ratio
 * @param packet_interval Time interval between packets (ms)
 */
void handleRFAnomaly(const String& vehicle_id, int16_t rssi, int8_t snr, int16_t packet_interval);

/**
 * Push a feature sample for RF training
 * @param vehicle_id Vehicle identifier
 * @param rssi Signal strength (RSSI)
 * @param snr Signal-to-noise ratio
 * @param packet_ts Packet timestamp
 */
void pushFeatureSample(const String& vehicle_id, int16_t rssi, int8_t snr, uint32_t packet_ts);

#endif
