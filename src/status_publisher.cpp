#include "status_publisher.h"
#include "system_state.h"
#include "config.h"
#include "mqtt_client.h"
#include "gps.h"
#include "wifi_manager.h"

#include <unordered_map>
#include <string>

// External declarations (defined in main.cpp)
extern std::unordered_map<std::string, VehicleStats> vehicleStats;

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

    for (auto const& item : vehicleStats) {
        const String vehicleId(item.first.c_str());  
        uint32_t lastTelemetryTs = item.second.lastTelemetry;
        bool online = (now - lastTelemetryTs <= VEHICLE_OFFLINE_TIMEOUT_MS);

        int packetCount = item.second.packetCount;
        int packetCountTotal = item.second.totalCount;
        int rejectedCount = item.second.rejectedCount;

        int packetCountArr[2] = {packetCount, packetCountTotal};
        int rejectedCountArr[2] = {rejectedCount, 0};

        publishNodeStatus(vehicleId, now, wifiRssi, heapFree, packetCountArr, rejectedCountArr, online);
    }
}

/**
 * Publish gateway GPS status (latitude, longitude) if GPS fix available
 * Called periodically during main loop
 */
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
