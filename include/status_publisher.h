#ifndef STATUS_PUBLISHER_H
#define STATUS_PUBLISHER_H

#include <Arduino.h>

struct VehicleStats {
    int packetCount = 0;
    int totalCount = 0;
    int rejectedCount = 0;
    uint32_t lastTelemetry = 0;
};

#define MAX_VEHICLES 4

struct VehicleStatsFixed {
    VehicleStats stats[MAX_VEHICLES];  
    
    VehicleStats& getStats(int node_id) {
        if (node_id < 0 || node_id >= MAX_VEHICLES) {
            return stats[0];  
        }
        return stats[node_id];
    }
};

// External declaration of global vehicle stats
extern VehicleStatsFixed gVehicleStats;

void publishNodeStatusPing();
void publishGatewayGpsStatusIfNeeded();

#endif
