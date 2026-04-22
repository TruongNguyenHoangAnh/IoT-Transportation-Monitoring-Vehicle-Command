#pragma once
#ifndef RF_KNN_PREDICTOR_PER_VEHICLE_H
#define RF_KNN_PREDICTOR_PER_VEHICLE_H

#include <cstdint>
#include <array>
#include <algorithm>
#include <cmath>
#include <string>

// Shared RF sample structure
struct RFSample {
    int16_t rssi;      // Received Signal Strength (dBm)
    int16_t snr;       // Signal-to-Noise Ratio (dB * 10), range [-200, 150]
    uint16_t interval; // Packet interval (ms)
    uint8_t label;     // Classification label (0/1/2)
};

// Include per-vehicle training data
#include "rf_training_transport_1.h"
#include "rf_training_transport_2.h"

struct RFPrediction {
    uint8_t label;           // 0=NORMAL, 1=WARNING, 2=CRITICAL
    float score;             // Distance to k-th neighbor (lower is better)
    float confidence;        // Voting confidence (0-100%)
    uint8_t k_neighbors;     // k value used for this vehicle
};

class RFKnnPredictor {
public:
    // Normalization bounds
    static constexpr float RSSI_MIN = -120.0f;
    static constexpr float RSSI_MAX = -30.0f;
    static constexpr float SNR_MIN = -20.0f;
    static constexpr float SNR_MAX = 15.0f;
    static constexpr float INTERVAL_MIN = 800.0f;
    static constexpr float INTERVAL_MAX = 3000.0f;    
    // Precomputed ranges (compile-time constant)
    static constexpr float RSSI_RANGE = RSSI_MAX - RSSI_MIN;       
    static constexpr float SNR_RANGE = SNR_MAX - SNR_MIN;         
    static constexpr float INTERVAL_RANGE = INTERVAL_MAX - INTERVAL_MIN;  
    /**
     * Predict RF anomaly for a vehicle.
     * Routes to vehicle-specific model based on vehicle_id.
     */
    static RFPrediction predict(
        const std::string& vehicle_id,
        int16_t rssi,
        int16_t snr,     
        uint16_t interval
    ) {
        // Normalize features using precomputed ranges
        float rssi_norm = (rssi - RSSI_MIN) / RSSI_RANGE;
        float snr_norm = (snr / 10.0f - SNR_MIN) / SNR_RANGE;
        float interval_norm = (interval - INTERVAL_MIN) / INTERVAL_RANGE;
        
        // Clamp to [0, 1]
        rssi_norm = std::max(0.0f, std::min(1.0f, rssi_norm));
        snr_norm = std::max(0.0f, std::min(1.0f, snr_norm));
        interval_norm = std::max(0.0f, std::min(1.0f, interval_norm));
        
        // Route to vehicle-specific predictor
        if (vehicle_id == "Transport-1") {
            return predict_transport_1(rssi_norm, snr_norm, interval_norm);
        } else if (vehicle_id == "Transport-2") {
            return predict_transport_2(rssi_norm, snr_norm, interval_norm);
        } else {
            return {0, 999.0f, 0.0f, 0};
        }
    }

private:
    struct DistanceLabel {
        float distance;
        uint8_t label;
    };
    
    // Vehicle-specific predictors
    static RFPrediction predict_transport_1(float rssi, float snr, float interval) {
        return knn_predict<5>(RF_TRAINING_TRANSPORT_1, rssi, snr, interval);
    }
    
    static RFPrediction predict_transport_2(float rssi, float snr, float interval) {
        return knn_predict<5>(RF_TRAINING_TRANSPORT_2, rssi, snr, interval);
    }
      
    /**
     * Generic kNN prediction with template-based k value.
     */
    template<uint8_t K, size_t N>
    static RFPrediction knn_predict(
        const std::array<RFSample, N>& training_data,
        float rssi,
        float snr,
        float interval
    ) {
        // Calculate distances to all samples
        DistanceLabel distances[N];
        
        for (size_t i = 0; i < N; i++) {
            float r_norm = training_data[i].rssi;
            float s_norm = training_data[i].snr / 10.0f;
            float int_norm = training_data[i].interval;
            
            // Normalize training sample features
            r_norm = (r_norm - RSSI_MIN) / (RSSI_MAX - RSSI_MIN);
            s_norm = (s_norm - SNR_MIN) / (SNR_MAX - SNR_MIN);
            int_norm = (int_norm - INTERVAL_MIN) / (INTERVAL_MAX - INTERVAL_MIN);
            
            r_norm = std::max(0.0f, std::min(1.0f, r_norm));
            s_norm = std::max(0.0f, std::min(1.0f, s_norm));
            int_norm = std::max(0.0f, std::min(1.0f, int_norm));
            
            // Euclidean distance
            float dr = rssi - r_norm;
            float ds = snr - s_norm;
            float di = interval - int_norm;
            float dist = std::sqrt(dr*dr + ds*ds + di*di);
            
            distances[i] = {dist, training_data[i].label};
        }
        
        // Sort by distance
        std::sort(distances, distances + N, [](const DistanceLabel& a, const DistanceLabel& b) {
            return a.distance < b.distance;
        });
        
        // Vote for k nearest neighbors
        uint16_t votes[3] = {0, 0, 0};
        float k_distance = 0.0f;
        
        for (uint8_t i = 0; i < K && i < N; i++) {
            votes[distances[i].label]++;
            if (i == K - 1) {
                k_distance = distances[i].distance;
            }
        }
        
        // Determine prediction
        uint8_t predicted_label = 0;
        uint16_t max_votes = votes[0];
        
        if (votes[1] > max_votes) {
            predicted_label = 1;
            max_votes = votes[1];
        }
        if (votes[2] > max_votes) {
            predicted_label = 2;
            max_votes = votes[2];
        }
        
        // Calculate confidence (percentage of votes)
        float confidence = (max_votes * 100.0f) / K;
        
        return {
            predicted_label,
            k_distance,
            confidence,
            K
        };
    }
};

#endif  // RF_KNN_PREDICTOR_PER_VEHICLE_H
