#include "rf_anomaly_handler.h"
#include "RFKnnPredictorPerVehicle.h"
#include "rf_anomaly_state.h"
#include "mqtt_client.h"
#include "feature_buffer.h"
#include "config.h"

/**
 * Handle RF anomaly detection based on RSSI, SNR, and packet interval
 * Updates RF state and publishes anomaly if detected
 */
void handleRFAnomaly(const String& vehicle_id, int16_t rssi, int8_t snr, int16_t packet_interval) {
    RFPrediction pred = RFKnnPredictor::predict(
        vehicle_id.c_str(),
        rssi,
        snr,
        packet_interval
    );
    
    const char* label_names[] = {"NORMAL", "WARNING", "CRITICAL"};
    String label_str = label_names[pred.label];
    
    rfState[vehicle_id].state = label_str;
    rfState[vehicle_id].score = pred.score;
    rfState[vehicle_id].confidence = pred.confidence;
    
    uint32_t now = millis();
    uint32_t last_publish = rfState[vehicle_id].lastPublish;
    
    bool should_publish = false;
    if (pred.label != 0) {  
        should_publish = true;
        if (now - last_publish < 5000) {
            should_publish = false;
        }
    }
    
    if (should_publish && isMQTTConnected()) {
        rfState[vehicle_id].lastPublish = now;
        
        if (pred.label == 1) {
            publishAnomaly(vehicle_id, "RF_WARNING", pred.score, 0.0694f);
            LOG_SERIAL("[RF-PER-VEHICLE] %s: WARNING detected (k=%d, score=%.4f, conf=%.1f%%)", 
                vehicle_id.c_str(), pred.k_neighbors, pred.score, pred.confidence);
        } else if (pred.label == 2) {
            publishAnomaly(vehicle_id, "RF_CRITICAL", pred.score, 0.0970f);
            LOG_SERIAL("[RF-PER-VEHICLE] %s: CRITICAL detected (k=%d, score=%.4f, conf=%.1f%%)", 
                vehicle_id.c_str(), pred.k_neighbors, pred.score, pred.confidence);
        }
    }
}

/**
 * Push feature sample for RF training and trigger anomaly detection
 * Maintains sliding window of recent samples
 */
void pushFeatureSample(const String& vehicle_id, int16_t rssi, int8_t snr, uint32_t packet_ts) {
    uint32_t now = millis();
    auto& samples = featureSamples[vehicle_id];
    samples.push_back({now, rssi, snr, packet_ts});
    
    int16_t packet_interval = 1000;  
    if (samples.size() >= 2) {
        uint32_t prev_ts = samples[samples.size() - 2].packet_ts;
        uint32_t curr_ts = packet_ts;
        if (curr_ts > prev_ts) {
            packet_interval = (int16_t)((curr_ts - prev_ts) / 1000); 
            if (packet_interval < 800) packet_interval = 800;
            if (packet_interval > 3000) packet_interval = 3000;
        }
    }
    
    handleRFAnomaly(vehicle_id, rssi, snr, packet_interval);

    uint32_t expire_before = now - FEATURE_WINDOW_MS;
    while (!samples.empty() && samples.front().received_at_ms < expire_before) {
        samples.erase(samples.begin());
    }
}
