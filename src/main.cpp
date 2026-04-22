#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "esp_log.h"

#include <algorithm>
#include <numeric>
#include <vector>
#include <map>
#include <unordered_map>
#include <string>

#include "config.h"
#include "serial_reader.h"
#include "json_parser.h"
#include "crypto_utils.h"
#include "mqtt_client.h"
#include "firestore_client.h"
#include "knn_anomaly_detector.h"
#include "gps.h"
#include "RFKnnPredictorPerVehicle.h"
#include "system_state.h"
#include "rf_anomaly_state.h"
#include "feature_buffer.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "config_storage.h"
#include "rf_anomaly_handler.h"
#include "status_publisher.h"
#include "knn_detection_handler.h"


KnnAnomalyDetector knnDetector;

std::unordered_map<std::string, VehicleStats> vehicleStats;

static bool isBase64Payload(const String& payload) {
    String trimmed = payload;
    trimmed.trim();
    if (trimmed.length() == 0 || (trimmed.length() % 4) != 0) {
        return false;
    }
    for (size_t i = 0; i < trimmed.length(); ++i) {
        char c = trimmed[i];
        if (!(isalnum(c) || c == '+' || c == '/' || c == '=')) {
            return false;
        }
    }
    return true;
}

bool verifySignedJson(const String &signedJson, String &outPlainJson) {
    DynamicJsonDocument doc(512);
    auto err = deserializeJson(doc, signedJson);
    if (err) {
        LOG_ERROR("[SEC] JSON parse failed in verifySignedJson: %s", err.c_str());
        return false;
    }

    if (!doc.containsKey("sig")) {
        LOG_ERROR("[SEC] Missing sig field in signed JSON");
        return false;
    }

    String signature = doc["sig"].as<String>();
    doc.remove("sig");

    String payload;
    payload.reserve(128);
    payload = String("{\"v\":\"") + doc["v"].as<const char*>() +
              String("\",\"ts\":") + String((uint32_t)doc["ts"].as<uint32_t>()) +
              String(",\"t\":") + String(doc["t"].as<float>(), 1) +
              String(",\"h\":") + String(doc["h"].as<float>(), 1) +
              String(",\"a\":") + String(doc["a"].as<float>(), 2) +
              String(",\"l\":") + String((uint32_t)doc["l"].as<uint32_t>()) +
              String(",\"x\":") + String((int)doc["x"].as<int>()) +
              String(",\"la\":") + String(doc["la"].as<double>(), 5) +
              String(",\"lo\":") + String(doc["lo"].as<double>(), 5) +
              String("}");

    char expectedSig[65];
    if (!hmacSha256(payload.c_str(), payload.length(), expectedSig)) {
        LOG_ERROR("[SEC] HMAC generation failed");
        return false;
    }
    if (strcmp(expectedSig, signature.c_str()) != 0) {
        LOG_ERROR("[SEC] HMAC mismatch: expected=%s received=%s", expectedSig, signature.c_str());
        return false;
    }

    outPlainJson = payload;
    return true;
}

void publishGatewayGpsStatusIfNeeded();
void publishNodeStatusPing();
void handleSerialData();
void blinkLED();

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(1000); 
    
    esp_log_level_set("wifi", ESP_LOG_NONE);
    esp_log_level_set("WiFiGeneric", ESP_LOG_NONE);
    esp_log_level_set("wifi_init", ESP_LOG_NONE);
    esp_log_level_set("system_api", ESP_LOG_NONE);
    esp_log_level_set("esp_netif", ESP_LOG_NONE);
    
    LOG_INFO("RX_ESP32_Firestore starting - Production WiFi Config System");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    WiFi.mode(WIFI_OFF);       
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    delay(500); 
    
    WiFiManager::init();
    ConfigStorage::debugDump();
    WebServer::init(WIFI_AP_PORT);
    
    initSerialReader();
    initGps();
    initMQTT();

    LOG_INFO("Setup complete - WiFi AP/STA system ready");
}

void loop() {
    handleSerialData();
    
    WiFiManager::update();
    
    if (WiFiManager::isConnected()) {
        mqttLoop();
        updateGps();
        publishGatewayGpsStatusIfNeeded();
        publishNodeStatusPing();
    }
    
    blinkLED();
}

void publishGatewayGpsStatusIfNeeded();
void publishNodeStatusPing();

void handleSerialData() {
    uint32_t now = millis();
    
    String line;
    if (!readRXLine(line)) {
        return;
    }

    RXPacket rxPacket;
    if (line.startsWith("[RX OK]")) {
        if (!parseRXLine(line, rxPacket)) {
            LOG_ERROR("Failed to parse valid RX line");
            return;
        }
    } else {
        String rawPayload = extractPayload(line);
        if (rawPayload.length() == 0) {
            rawPayload = line;
        }
        rawPayload.trim();

        if (rawPayload.length() == 0 ||
            !(rawPayload.startsWith("{") || isBase64Payload(rawPayload))) {
            return;
        }

        rxPacket.payload = rawPayload;
        rxPacket.node_id = 0;
    }

    // Decrypt payload if needed
    if (!decryptPayload(rxPacket)) {
        return;
    }

    // Verify HMAC signature
    String unsignedJson;
    if (!verifySignedJson(rxPacket.payload, unsignedJson)) {
        LOG_ERROR("HMAC verification failed for RX payload");
        String vehicle_id;
        if (rxPacket.node_id == 1) vehicle_id = "Transport-1";
        if (rxPacket.node_id == 2) vehicle_id = "Transport-2";
        if (vehicle_id.length() > 0) {
            vehicleStats[vehicle_id.c_str()].rejectedCount += 1;
        }
        return;
    }
    rxPacket.payload = unsignedJson;

    // Validate packet structure
    if (!validateRXPacket(rxPacket)) {
        LOG_ERROR("RX packet validation failed");
        String vehicle_id;
        if (rxPacket.node_id == 1) vehicle_id = "Transport-1";
        if (rxPacket.node_id == 2) vehicle_id = "Transport-2";
        if (vehicle_id.length() > 0) {
            vehicleStats[vehicle_id.c_str()].rejectedCount += 1;
        }
        return;
    }

    // Print RX packet debug info
    debugPrintRXPacket(rxPacket);

    // Check for heartbeat
    String dummy_id;
    if (parseHeartbeat(rxPacket.payload, dummy_id)) {
        LOG_INFO("Heartbeat received from: %s", rxPacket.payload.c_str());
        if (dummy_id.length() > 0) {
            vehicleStats[dummy_id.c_str()].packetCount += 1;
            vehicleStats[dummy_id.c_str()].totalCount += 1;
        }
        return;
    }

    // Parse telemetry
    TelemetryData telemetry;
    if (!parseJSONPayload(rxPacket.payload, telemetry)) {
        LOG_ERROR("JSON parse failed");
        String vehicle_id = rxPacket.node_id == 2 ? String("Transport-2") : String("");
        if (vehicle_id.length() > 0) {
            vehicleStats[vehicle_id.c_str()].rejectedCount += 1;
        }
        return;
    }

    // Enrich telemetry with reception data
    enrichTelemetryData(telemetry, rxPacket.rssi, rxPacket.snr,
                      rxPacket.sequence, rxPacket.received_at_ms);

    // Push RF feature samples
    if (telemetry.vehicle_id.length() > 0) {
        pushFeatureSample(telemetry.vehicle_id, telemetry.rssi, telemetry.snr, telemetry.timestamp_ms);
    }

    // Run kNN-based ML detection
    runKnnDetection(telemetry, knnDetector);

    // Set RF Anomaly Detection results
    telemetry.rf_anomaly_state = "NORMAL";
    telemetry.rf_anomaly_score = 0.0f;
    telemetry.rf_anomaly_confidence = 0.0f;
    if (rfState.find(telemetry.vehicle_id) != rfState.end()) {
        telemetry.rf_anomaly_state = rfState[telemetry.vehicle_id].state;
        telemetry.rf_anomaly_score = rfState[telemetry.vehicle_id].score;
        telemetry.rf_anomaly_confidence = rfState[telemetry.vehicle_id].confidence;
    }

    // Publish to MQTT
    publishTelemetry(telemetry);

    // Update vehicle statistics
    if (telemetry.vehicle_id.length() > 0) {
        vehicleStats[telemetry.vehicle_id.c_str()].packetCount += 1;
        vehicleStats[telemetry.vehicle_id.c_str()].totalCount += 1;
        vehicleStats[telemetry.vehicle_id.c_str()].lastTelemetry = now;
    }
}

void blinkLED() {
    uint32_t now = millis();
    if (now - sys.lastBlinkTime > (sys.ledOn ? 100 : 900)) {
        sys.ledOn = !sys.ledOn;
        digitalWrite(LED_PIN, sys.ledOn ? HIGH : LOW);
        sys.lastBlinkTime = now;
    }
}


