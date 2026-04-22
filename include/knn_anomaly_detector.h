#ifndef KNN_ANOMALY_DETECTOR_H
#define KNN_ANOMALY_DETECTOR_H

#include <Arduino.h>
#include "training_data.h"

struct KnnResult {
    bool anomaly;
    float score;
    float threshold;
    const char* reason;
};

class KnnAnomalyDetector {
public:
    KnnAnomalyDetector(int k = 5);
    void init();
    KnnResult detect(const char* vehicle_id,
                     float temperature,
                     float humidity,
                     float accel) const;
    void reset(const char* vehicle_id);

private:
    struct TrainingSet {
        const char* id;
        const TrainingPacket* data;
        size_t count;
        float threshold;
        float min_temp;
        float max_temp;
        float min_hum;
        float max_hum;
        float min_accel;
        float max_accel;
    };

    static const size_t MAX_SETS = 5;
    TrainingSet sets[MAX_SETS];
    size_t setCount;
    int k_;

    void addSet(const char* id, const TrainingPacket* data, size_t count);
    const TrainingSet* findSet(const char* vehicle_id) const;
    float distance(const TrainingSet& set, const TrainingPacket& row,
                   float temperature,
                   float humidity,
                   float accel) const;
    float averageTopK(float values[], size_t n) const;
    float estimateThreshold(const TrainingSet& set) const;
    float normalizeFeature(float value, float min, float max) const;
};

#endif // KNN_ANOMALY_DETECTOR_H
