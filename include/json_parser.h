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
    uint8_t anomaly_flag;        // 0=NORMAL, 1=ANOMALY from hybrid ML decision
    float fused_anomaly_score;
    /* TinyML Neural Network Anomaly Detection */
    float nn_anomaly_score;      // Neural network probability [0.0-1.0]
    String nn_anomaly_state;     // NORMAL or ANOMALY
    
    TelemetryData() : temperature(0), humidity(0), accel_magnitude(0),
                      gps_latitude(0), gps_longitude(0), light_level(0),
                      tamper_flag(0), timestamp_ms(0), rssi(0), snr(0),
                      sequence(0), anomaly_flag(0),
                      nn_anomaly_score(0), nn_anomaly_state("NORMAL") {}
};

bool parseJSONPayload(const String& payload, TelemetryData& data);
bool parseHeartbeat(const String& payload, String& vehicle_id);

void enrichTelemetryData(TelemetryData& data, int16_t rssi, int8_t snr, 
                         uint32_t sequence, uint32_t timestamp_ms);

bool validateTelemetryData(const TelemetryData& data);

JsonObject telemetryToJson(const TelemetryData& data, JsonDocument& doc);

void debugPrintTelemetryData(const TelemetryData& data);

#endif // JSON_PARSER_H
