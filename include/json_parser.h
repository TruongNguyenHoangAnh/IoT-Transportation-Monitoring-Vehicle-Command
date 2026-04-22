#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <String.h>

struct TelemetryData {
    String vehicle_id;
    float temperature;          // Celsius
    float humidity;             // Percentage
    float accel_magnitude;      // g
    float gps_latitude;         // degrees
    float gps_longitude;        // degrees
    uint16_t light_level;       // ADC value (0-4095)
    uint8_t tamper_flag;        // 0 = normal, 1 = tamper detected
    String status;              // "OK", "ERROR", etc
    uint32_t timestamp_ms;      // From RX gateway
    int16_t rssi;               // Signal strength
    int8_t snr;                 // Signal-to-noise ratio
    uint32_t sequence;          // Packet sequence number
    String knn_prediction;       // NORMAL or ANOMALY from KNN outlier model
    String knn_reason;           // Feature reason: T/H/AG or NONE
    float knn_score;             // KNN anomaly score
    float knn_threshold;         // KNN threshold
    String rf_anomaly_state;     // NORMAL, WARNING, CRITICAL
    float rf_anomaly_score;      // Distance to 9th nearest neighbor
    float rf_anomaly_confidence; // Voting confidence percentage
    
    TelemetryData() : temperature(0), humidity(0), accel_magnitude(0),
                      gps_latitude(0), gps_longitude(0), light_level(0),
                      tamper_flag(0), timestamp_ms(0), rssi(0), snr(0),
                      sequence(0), knn_prediction("UNKNOWN"), knn_reason("NONE"), knn_score(0), knn_threshold(0),
                      rf_anomaly_state("NORMAL"), rf_anomaly_score(0), rf_anomaly_confidence(0) {}
};

bool parseJSONPayload(const String& payload, TelemetryData& data);
bool parseHeartbeat(const String& payload, String& vehicle_id);

void enrichTelemetryData(TelemetryData& data, int16_t rssi, int8_t snr, 
                         uint32_t sequence, uint32_t timestamp_ms);

bool validateTelemetryData(const TelemetryData& data);

JsonObject telemetryToJson(const TelemetryData& data, JsonDocument& doc);

void debugPrintTelemetryData(const TelemetryData& data);

#endif // JSON_PARSER_H
