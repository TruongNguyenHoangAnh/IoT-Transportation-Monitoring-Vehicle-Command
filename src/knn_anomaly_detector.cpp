#include "knn_anomaly_detector.h"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstring>

KnnAnomalyDetector::KnnAnomalyDetector(int k)
    : setCount(0),
      k_(k) {
    addSet("Transport-1", TRAINING_DATA_TRANSPORT1.data(), TRAINING_DATA_TRANSPORT1.size());
    addSet("Transport-2", TRAINING_DATA_TRANSPORT2.data(), TRAINING_DATA_TRANSPORT2.size());
    init();
}

void KnnAnomalyDetector::init() {
    for (size_t i = 0; i < setCount; i++) {
        const auto& set = sets[i];
        for (size_t j = 0; j < set.count; j++) {
            const auto& row = set.data[j];
            sets[i].min_temp = std::min(sets[i].min_temp, row.temperature);
            sets[i].max_temp = std::max(sets[i].max_temp, row.temperature);
            sets[i].min_hum = std::min(sets[i].min_hum, row.humidity);
            sets[i].max_hum = std::max(sets[i].max_hum, row.humidity);
            sets[i].min_accel = std::min(sets[i].min_accel, row.accel_magnitude);
            sets[i].max_accel = std::max(sets[i].max_accel, row.accel_magnitude);
        }
        sets[i].min_accel = std::min(sets[i].min_accel, 0.0f);
        sets[i].threshold = estimateThreshold(sets[i]);
    }
}

void KnnAnomalyDetector::addSet(const char* id, const TrainingPacket* data, size_t count) {
    if (setCount >= MAX_SETS) {
        return;
    }
    sets[setCount].id = id;
    sets[setCount].data = data;
    sets[setCount].count = count;
    sets[setCount].threshold = 0.0;
    sets[setCount].min_temp = FLT_MAX;
    sets[setCount].max_temp = -FLT_MAX;
    sets[setCount].min_hum = FLT_MAX;
    sets[setCount].max_hum = -FLT_MAX;
    sets[setCount].min_accel = FLT_MAX;
    sets[setCount].max_accel = -FLT_MAX;
    setCount++;
}

const KnnAnomalyDetector::TrainingSet* KnnAnomalyDetector::findSet(const char* vehicle_id) const {
    if (!vehicle_id || vehicle_id[0] == '\0') {
        return nullptr;
    }
    for (size_t i = 0; i < setCount; i++) {
        if (strcmp(sets[i].id, vehicle_id) == 0) {
            return &sets[i];
        }
    }
    return nullptr;
}

float KnnAnomalyDetector::normalizeFeature(float value, float min, float max) const {
    if (max <= min) {
        return 0.0f;
    }
    return (value - min) / (max - min);
}

float KnnAnomalyDetector::distance(const TrainingSet& set, const TrainingPacket& row,
                                   float temperature,
                                   float humidity,
                                   float accel) const {
    float temp_norm = normalizeFeature(temperature, set.min_temp, set.max_temp);
    float hum_norm = normalizeFeature(humidity, set.min_hum, set.max_hum);
    float accel_norm = normalizeFeature(accel, set.min_accel, set.max_accel);

    float row_temp_norm = normalizeFeature(row.temperature, set.min_temp, set.max_temp);
    float row_hum_norm = normalizeFeature(row.humidity, set.min_hum, set.max_hum);
    float row_accel_norm = normalizeFeature(row.accel_magnitude, set.min_accel, set.max_accel);

    float dt = temp_norm - row_temp_norm;
    float dh = hum_norm - row_hum_norm;
    float da = accel_norm - row_accel_norm;
    return sqrt(dt * dt + dh * dh + da * da);
}

float KnnAnomalyDetector::averageTopK(float values[], size_t n) const {
    size_t k = (size_t)k_;
    if (k == 0 || n == 0) {
        return 0.0;
    }
    if (k > n) {
        k = n;
    }
    std::sort(values, values + n);
    float sum = 0.0;
    for (size_t i = 0; i < k; i++) {
        sum += values[i];
    }
    return sum / k;
}

float KnnAnomalyDetector::estimateThreshold(const TrainingSet& set) const {
    if (set.count == 0) {
        return 0.0;
    }

    float scores[200];
    for (size_t i = 0; i < set.count; i++) {
        float dists[200];
        size_t idx = 0;
        for (size_t j = 0; j < set.count; j++) {
            if (i == j) {
                continue;
            }
            dists[idx++] = distance(set, set.data[j],
                                   set.data[i].temperature,
                                   set.data[i].humidity,
                                   set.data[i].accel_magnitude);
        }
        scores[i] = averageTopK(dists, idx);
    }

    std::sort(scores, scores + set.count);
    size_t index = (set.count * 980) / 1000;
    if (index >= set.count) {
        index = set.count - 1;
    }
    const float threshold_multiplier = 1.2;
    return scores[index] * threshold_multiplier;
}

KnnResult KnnAnomalyDetector::detect(const char* vehicle_id,
                                     float temperature,
                                     float humidity,
                                     float accel) const {
    const TrainingSet* set = findSet(vehicle_id);
    if (!set) {
        set = &sets[0];
    }
    if (!set || set->count == 0) {
        return {false, 0.0, 0.0, "Normal"};
    }

    float dists[200];
    float best_dt = 0.0f;
    float best_dh = 0.0f;
    float best_da = 0.0f;
    float best_dist = FLT_MAX;

    float temp_norm = normalizeFeature(temperature, set->min_temp, set->max_temp);
    float hum_norm = normalizeFeature(humidity, set->min_hum, set->max_hum);
    float accel_norm = normalizeFeature(accel, set->min_accel, set->max_accel);

    for (size_t i = 0; i < set->count; i++) {
        const TrainingPacket& row = set->data[i];
        float row_temp_norm = normalizeFeature(row.temperature, set->min_temp, set->max_temp);
        float row_hum_norm = normalizeFeature(row.humidity, set->min_hum, set->max_hum);
        float row_accel_norm = normalizeFeature(row.accel_magnitude, set->min_accel, set->max_accel);

        float dt = fabs(temp_norm - row_temp_norm);
        float dh = fabs(hum_norm - row_hum_norm);
        float da = fabs(accel_norm - row_accel_norm);
        float dist = sqrt(dt * dt + dh * dh + da * da);
        dists[i] = dist;

        if (dist < best_dist) {
            best_dist = dist;
            best_dt = dt;
            best_dh = dh;
            best_da = da;
        }
    }
    float score = averageTopK(dists, set->count);
    bool anomaly = score > set->threshold;
    const char* reason = "N";
    if (anomaly) {
        if (best_dt >= best_dh && best_dt >= best_da) {
            reason = "T";
        } else if (best_dh >= best_da) {
            reason = "H";
        } else {
            reason = "AG";
        }
    }
    return {anomaly, score, set->threshold, reason};
}

void KnnAnomalyDetector::reset(const char* vehicle_id) {
    const TrainingSet* set = findSet(vehicle_id);
    if (!set) {
        return;
    }
}
