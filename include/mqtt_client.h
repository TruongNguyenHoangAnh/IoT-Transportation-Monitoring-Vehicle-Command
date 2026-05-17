#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <Arduino.h>
#include <deque>
#include <map>
#include <AsyncMqttClient.h>

#define MQTT_MAX_PACKET_SIZE 1024

#include "json_parser.h"

struct OutgoingMessage {
    String topic;
    String payload;
    bool retain;
    int qos;
};

void initMQTT();

void mqttLoop();

void publishTelemetry(const TelemetryData& data);

void publishNodeStatus(const String& vehicle_id, uint32_t uptime_ms, int wifi_rssi,
                       int heap_free, int packet_count[2], int rejected_count[2], bool online);

void publishGatewayGpsStatus(double gps_latitude, double gps_longitude);

bool isMQTTConnected();

bool publishWithRetry(const String& topic, const String& payload, int qos = 1, bool retain = false);

bool publishQoS1(const String& topic, const String& payload, bool retain = false);

void savePendingPublishQueue();

void loadPendingPublishQueue();

void flushPendingPublishes();

void reconnectMQTT();

void resetMQTTBackoff();

#endif 