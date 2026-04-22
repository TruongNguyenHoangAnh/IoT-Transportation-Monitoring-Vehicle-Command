#ifndef KNN_DETECTION_HANDLER_H
#define KNN_DETECTION_HANDLER_H

#include <Arduino.h>
#include "json_parser.h"
#include "knn_anomaly_detector.h"

// Run kNN detection on telemetry data
void runKnnDetection(TelemetryData& telemetry, KnnAnomalyDetector& detector);

#endif // KNN_DETECTION_HANDLER_H
