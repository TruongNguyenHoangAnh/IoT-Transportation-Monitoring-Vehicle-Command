#ifndef STATUS_PUBLISHER_H
#define STATUS_PUBLISHER_H

#include <Arduino.h>

/**
 * Vehicle statistics structure for tracking packet counts
 */
struct VehicleStats {
    int packetCount = 0;
    int totalCount = 0;
    int rejectedCount = 0;
    uint32_t lastTelemetry = 0;
};

/**
 * Publish node status ping with packet counts and connection info
 * Throttled to NODE_STATUS_INTERVAL
 */
void publishNodeStatusPing();

/**
 * Publish gateway GPS status if GPS fix is available
 * Throttled to GATEWAY_GPS_STATUS_INTERVAL
 */
void publishGatewayGpsStatusIfNeeded();

#endif
