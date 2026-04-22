#include "feature_buffer.h"

std::map<String, std::vector<FeatureSample>> featureSamples;

void cleanupFeatureSamples(const String& vehicle_id, uint32_t window_ms) {
    auto it = featureSamples.find(vehicle_id);
    if (it == featureSamples.end()) {
        return;
    }
    
    auto& samples = it->second;
    uint32_t now = millis();
    uint32_t expire_before = now - window_ms;
    
    while (!samples.empty() && samples.front().received_at_ms < expire_before) {
        samples.erase(samples.begin());
    }
}
