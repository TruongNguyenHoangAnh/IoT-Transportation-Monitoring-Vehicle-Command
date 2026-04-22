#include "knn_detection_handler.h"
#include "mqtt_client.h"

void runKnnDetection(TelemetryData& telemetry, KnnAnomalyDetector& detector) {
    float knn_accel = telemetry.accel_magnitude;
    if (knn_accel < 0.0) {
        knn_accel = 0.0;
    }

    uint32_t t0 = micros();
    KnnResult res = detector.detect(
        telemetry.vehicle_id.c_str(),
        telemetry.temperature,
        telemetry.humidity,
        knn_accel
    );
    uint32_t us = micros() - t0;
    Serial.printf("[Profile] kNN detect: %.2f ms\n", us/1000.0f);

    telemetry.knn_score = res.score;
    telemetry.knn_threshold = res.threshold;
    telemetry.knn_prediction = res.anomaly ? String("ANOMALY") : String("NORMAL");

    if (res.anomaly) {
        publishAnomaly(telemetry.vehicle_id, res.reason, res.score, res.threshold);
    }
}
