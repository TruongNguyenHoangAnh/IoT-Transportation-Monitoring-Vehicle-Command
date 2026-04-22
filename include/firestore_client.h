#ifndef FIRESTORE_CLIENT_H
#define FIRESTORE_CLIENT_H

#include <Arduino.h>
#include <String.h>
#include "json_parser.h"

// Dummy structures
struct PendingUpload {
    String document_path;
    String json_data;
};

struct NodeStatistics {
    String vehicle_id;
};

inline bool initFirestore() { return true; }
inline bool isFirestoreReady() { return true; }
inline bool uploadTelemetry(const TelemetryData& data) { return true; }
inline bool updateStatistics(const String& vehicle_id, const TelemetryData& data) { return true; }
inline bool queueUpload(const String& path, const String& json_data) { return true; }
inline void processPendingQueue() {}
inline int getPendingQueueSize() { return 0; }
inline void clearPendingQueue() {}
inline void printFirebaseStatus() { Serial.println("[FB] Using Firebase Realtime Database via HTTP"); }
inline void forceSync() {}

#endif // FIRESTORE_CLIENT_H
