#include "json_parser.h"
#include "config.h"

bool parseJSONPayload(const String& payload, TelemetryData& data) {
    LOG_JSON("Parsing payload: %s", payload.c_str());
    
    if (payload.startsWith("HEARTBEAT")) {
        // Heartbeat payloads are handled separately by parseHeartbeat().
        LOG_JSON("Heartbeat passed to JSON parser");
        return false;
    }
    
    StaticJsonDocument<256> doc;
    
    // Deserialize JSON
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        LOG_ERROR("JSON parse error: %s", error.c_str());
        return false;
    }
    
    // Extract vehicle_id (chấp nhận cả 'vehicle_id' hoặc 'v')
    if (doc.containsKey("vehicle_id")) {
        data.vehicle_id = doc["vehicle_id"].as<String>();
    } else if (doc.containsKey("v")) {
        data.vehicle_id = doc["v"].as<String>();
    } else {
        LOG_ERROR("Missing vehicle_id in payload");
        return false;
    }
    
    // Extract timestamp (ts hoặc timestamp)
    if (doc.containsKey("ts")) {
        data.timestamp_ms = doc["ts"].as<uint32_t>();
    } else if (doc.containsKey("timestamp")) {
        data.timestamp_ms = doc["timestamp"].as<uint32_t>();
    } else if (doc.containsKey("timestamp_ms")) {
        data.timestamp_ms = doc["timestamp_ms"].as<uint32_t>();
    }

    // Extract temperature (t hoặc temp)
    if (doc.containsKey("t")) {
        data.temperature = doc["t"].as<float>();
    } else if (doc.containsKey("temp")) {
        data.temperature = doc["temp"].as<float>();
    }

    // Extract humidity (h hoặc hum hoặc humidity)
    if (doc.containsKey("h")) {
        data.humidity = doc["h"].as<float>();
    } else if (doc.containsKey("hum")) {
        data.humidity = doc["hum"].as<float>();
    } else if (doc.containsKey("humidity")) {
        data.humidity = doc["humidity"].as<float>();
    }

    // Extract status
    if (doc.containsKey("status")) {
        data.status = doc["status"].as<String>();
    }

    if (doc.containsKey("a")) {
        data.accel_magnitude = doc["a"].as<float>();
    } else if (doc.containsKey("accel_mag")) {
        data.accel_magnitude = doc["accel_mag"].as<float>();
    } else if (doc.containsKey("accel_magnitude")) {
        data.accel_magnitude = doc["accel_magnitude"].as<float>();
    }

    if (doc.containsKey("l")) {
        data.light_level = doc["l"].as<uint16_t>();
    } else if (doc.containsKey("light_level")) {
        data.light_level = doc["light_level"].as<uint16_t>();
    }

    if (doc.containsKey("x")) {
        data.tamper_flag = doc["x"].as<uint8_t>();
    } else if (doc.containsKey("tamper")) {
        data.tamper_flag = doc["tamper"].as<uint8_t>();
    }

    if (doc.containsKey("la")) {
        data.gps_latitude = doc["la"].as<float>();
    }
    if (doc.containsKey("lo")) {
        data.gps_longitude = doc["lo"].as<float>();
    }

    if (doc.containsKey("gps")) {
        JsonObject gps = doc["gps"].as<JsonObject>();
        if (gps.containsKey("lat")) {
            data.gps_latitude = gps["lat"].as<float>();
        }
        if (gps.containsKey("lng")) {
            data.gps_longitude = gps["lng"].as<float>();
        }
    }

    LOG_JSON("Parse successful - Vehicle: %s, Temp: %.1f, Timestamp: %lu",
             data.vehicle_id.c_str(), data.temperature, (unsigned long)data.timestamp_ms);
    
    return true;
}

bool parseHeartbeat(const String& payload, String& vehicle_id) {
    if (!payload.startsWith("HEARTBEAT")) {
        return false;
    }
    
    int colonIdx = payload.indexOf(":");
    if (colonIdx != -1) {
        vehicle_id = payload.substring(colonIdx + 1);
        vehicle_id.trim();
        LOG_JSON("Heartbeat from: %s", vehicle_id.c_str());
        return true;
    }
    
    return false;
}

void enrichTelemetryData(TelemetryData& data, int16_t rssi, int8_t snr, 
                         uint32_t sequence, uint32_t timestamp_ms) {
    data.rssi = rssi;
    data.snr = snr;
    data.sequence = sequence;
    if (data.timestamp_ms == 0) {
        data.timestamp_ms = timestamp_ms;
    }
    LOG_JSON("Enriched telemetry - RSSI: %d, SNR: %d, Seq: %d", rssi, snr, sequence);
}

bool validateTelemetryData(const TelemetryData& data) {
    if (data.vehicle_id.length() == 0) {
        LOG_ERROR("Validation failed: empty vehicle_id");
        return false;
    }
    
    if (data.temperature < -50 || data.temperature > 100) {
        LOG_ERROR("Validation warning: temperature out of range: %.1f", data.temperature);
    }
    
    if (data.humidity < 0 || data.humidity > 100) {
        LOG_ERROR("Validation warning: humidity out of range: %.1f", data.humidity);
    }
    
    if (data.rssi > 0) {
        LOG_ERROR("Validation warning: RSSI value seems incorrect: %d", data.rssi);
    }
    
    LOG_JSON("Validation passed for: %s", data.vehicle_id.c_str());
    return true;
}

JsonObject telemetryToJson(const TelemetryData& data, JsonDocument& doc) {
    JsonObject root = doc.to<JsonObject>();
    
    root["vehicle_id"] = data.vehicle_id;
    root["temperature"] = (int)round(data.temperature * 10.0f);
    root["humidity"] = (int)round(data.humidity * 10.0f);
    root["status"] = data.status;
    root["accel_magnitude"] = (int)round(data.accel_magnitude * 100.0f);


    JsonObject gps = root.createNestedObject("gps");
    gps["latitude"] = data.gps_latitude;
    gps["longitude"] = data.gps_longitude;

    root["light_level"] = data.light_level;
    root["tamper"] = data.tamper_flag;
    root["timestamp_ms"] = data.timestamp_ms;
    root["anomaly_flag"] = data.anomaly_flag;
    
    JsonObject gateway = root.createNestedObject("gateway");
    gateway["rssi"] = data.rssi;
    gateway["snr"] = data.snr;
    gateway["sequence"] = data.sequence;
    
    return root;
}

void debugPrintTelemetryData(const TelemetryData& data) {
    LOG_INFO("===== TELEMETRY DATA =====");
    LOG_INFO("Vehicle ID: %s", data.vehicle_id.c_str());
    LOG_INFO("Status: %s", data.status.c_str());
    LOG_INFO("Temperature: %.1f°C", data.temperature);
    LOG_INFO("Humidity: %.1f%%", data.humidity);
    LOG_INFO("Acceleration: %.2f m/s²", data.accel_magnitude);
    LOG_INFO("GPS: %.4f, %.4f", data.gps_latitude, data.gps_longitude);
    LOG_INFO("Light Level: %d", data.light_level);
    LOG_INFO("Tamper: %d", data.tamper_flag);
    LOG_INFO("Signal - RSSI: %d dBm, SNR: %d dB", data.rssi, data.snr);
    LOG_INFO("Anomaly Flag: %d", data.anomaly_flag);
    LOG_INFO("Sequence: %d", data.sequence);
    LOG_INFO("Timestamp: %d ms", data.timestamp_ms);
    LOG_INFO("==========================");
}
