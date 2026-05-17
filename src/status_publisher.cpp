#include "status_publisher.h"
#include "system_state.h"
#include "config.h"
#include "mqtt_client.h"
#include "gps.h"
#include "wifi_manager.h"

/**
 * Publish node status ping with WiFi RSSI, heap, and packet statistics
 * Called periodically during main loop
 */
void publishNodeStatusPing() {
    uint32_t now = millis();
    if (now - sys.lastNodeStatusTime < NODE_STATUS_INTERVAL) {
        return;
    }
    sys.lastNodeStatusTime = now;

    int wifiRssi = WiFiManager::isConnected() ? WiFi.RSSI() : INT_MIN;
    int heapFree = ESP.getFreeHeap();

    // Iterate through fixed vehicle slots
    for (int node_id = 0; node_id < MAX_VEHICLES; node_id++) {
        // Generate vehicle ID string based on node_id
        String vehicleId;
        switch (node_id) {
            case 0: vehicleId = "Unknown"; break;
            case 1: vehicleId = "Transport-1"; break;
            case 2: vehicleId = "Transport-2"; break;
            default: vehicleId = String("Node-") + node_id; break;
        }

        VehicleStats& stats = gVehicleStats.getStats(node_id);
        
        // Skip Unknown vehicle or vehicle that never received telemetry
        if (vehicleId == "Unknown" || stats.lastTelemetry == 0) {
            continue;  // Don't publish status for inactive vehicles
        }
        
        uint32_t lastTelemetryTs = stats.lastTelemetry;
        // online = true if telemetry received recently (within offline timeout)
        bool online = (now - lastTelemetryTs <= VEHICLE_OFFLINE_TIMEOUT_MS);

        int packetCount = stats.packetCount;
        int packetCountTotal = stats.totalCount;
        int rejectedCount = stats.rejectedCount;

        int packetCountArr[2] = {packetCount, packetCountTotal};
        int rejectedCountArr[2] = {rejectedCount, 0};

        publishNodeStatus(vehicleId, now, wifiRssi, heapFree, packetCountArr, rejectedCountArr, online);
    }
}

void publishGatewayGpsStatusIfNeeded() {
    uint32_t now = millis();
    if (now - sys.lastGatewayGpsStatusTime < GATEWAY_GPS_STATUS_INTERVAL) {
        return;
    }
    sys.lastGatewayGpsStatusTime = now;

    if (!hasGpsFix()) {
        LOG_WARN("[GPS] No fix yet, skipping gateway GPS publish");
        return;
    }

    double latitude = getGpsLatitude();
    double longitude = getGpsLongitude();
    publishGatewayGpsStatus(latitude, longitude);
}
