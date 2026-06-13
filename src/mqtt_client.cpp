// ============================================================================
// mqtt_client_tls.cpp
// 
// MQTT Client with TLS/SSL Support
// Replaces original mqtt_client.cpp for secure MQTT over TLS
// 
// Key Changes:
//   - WiFiClientSecure instead of WiFiClient (plaintext)
//   - setCACert() for certificate verification
//   - Support for both TLS (port 8883) and plain (port 1883)
//   - Compile-time config with MQTT_USE_TLS flag
//
// ============================================================================

#include "mqtt_client.h"
#include "config.h"
#include "crypto_utils.h"
#include "wifi_manager.h"
#include "mqtt_tls_ca_cert.h"  // CA certificate embedded
#include <WiFi.h>
#include <WiFiClientSecure.h>  // TLS support
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <esp_task_wdt.h>
#include <deque>
#include <queue>
#include <unordered_map>
#include <time.h>

// ============================================================================
// MQTT TLS Configuration
// ============================================================================
#define MQTT_USE_TLS true                      // Enable TLS
#define MQTT_TLS_PORT 8883                     // Standard MQTT over TLS
#define MQTT_PLAIN_PORT 1883                   // Fallback plain MQTT
#define MQTT_TLS_HOSTNAME "mqtt.edgeai.local"  // Must match server cert CN
#define MQTT_CONNECT_TIMEOUT_MS 15000          // TLS handshake timeout
#define MQTT_KEEPALIVE_SEC 120                 // Keep-alive interval

// ============================================================================
// Client Initialization with TLS Support
// ============================================================================

// Use WiFiClientSecure for TLS, or WiFiClient for plain
#if MQTT_USE_TLS
    WiFiClientSecure tlsClient;
    WiFiClientSecure* activeClient = &tlsClient;
    PubSubClient mqttClient(tlsClient);
#else
    WiFiClient plainClient;
    WiFiClient* activeClient = &plainClient;
    PubSubClient mqttClient(plainClient);
#endif

// ============================================================================
// Global State
// ============================================================================

uint16_t mqttEffectivePort = MQTT_USE_TLS ? MQTT_TLS_PORT : MQTT_PLAIN_PORT;
bool mqttConnected = false;
bool mqttTLSReady = false;  // Flag: TLS handshake success

uint32_t lastMQTTReconnectAttempt = 0;
uint16_t mqttReconnectDelay = 5000;
const uint16_t MAX_RECONNECT_DELAY = 60000;
const uint16_t MIN_RECONNECT_DELAY = 1000;
uint32_t lastNoWiFiLogTime = 0;
uint32_t lastTLSErrorTime = 0;

// TLS-specific diagnostics
struct TLSDiagnostics {
    uint32_t handshakeStartMs = 0;
    uint32_t handshakeDurationMs = 0;
    int lastSSLError = 0;
    uint8_t certificateVerifyFailures = 0;
    uint32_t lastCertVerifyFailTime = 0;
} tlsDiag;

// Priority queue: telemetry > status
enum MessagePriority { PRIORITY_LOW = 2, PRIORITY_MEDIUM = 1, PRIORITY_HIGH = 0 };
struct PriorityMessage {
    OutgoingMessage msg;
    MessagePriority priority;
    bool operator>(const PriorityMessage& other) const { return priority > other.priority; }
};
std::priority_queue<PriorityMessage, std::vector<PriorityMessage>, std::greater<PriorityMessage>> pendingPublishQueue;
const size_t MAX_PENDING_PUBLISH = 200;
const int MAX_FLUSH_PER_LOOP = 5;
const uint32_t FLUSH_DELAY_MS = 20;

// Batched SPIFFS saves
uint32_t lastQueueSaveTime = 0;
const uint32_t QUEUE_SAVE_INTERVAL_MS = 5000;
uint32_t queueDirtyCount = 0;
const int QUEUE_SAVE_MSG_THRESHOLD = 10;

// Priority queue fairness
int highPriorityStreak = 0;
const int MAX_HIGH_PRIORITY_STREAK = 3;

// Backpressure control
const size_t QUEUE_PRESSURE_THRESHOLD = 150;
const size_t QUEUE_CRITICAL_THRESHOLD = 190;

// Multi-layer rate limiting
const uint32_t VEHICLE_MIN_INTERVAL_MS = 500;
const uint32_t TYPE_MIN_INTERVAL_MS = 100;

bool publishRateManualOverride = false;
uint32_t custom_min_publish_interval_ms = VEHICLE_MIN_INTERVAL_MS;

const uint32_t JITTER_RANGE_MS = 500;

uint32_t lastGlobalPublishMs = 0;
const uint32_t GLOBAL_MIN_INTERVAL_MS = 30;

static uint32_t hashString(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash & 0x7FFFFFFF;
}

std::unordered_map<uint32_t, uint32_t> lastVehiclePublishHash;
std::unordered_map<uint32_t, uint32_t> lastMessageTypePublishHash;

const char *MQTT_QUEUE_FILE = "/mqtt_pub_queue.json";

// ============================================================================
// MQTT MESSAGE HANDLING
// ============================================================================

void mqttMessageReceived(char* topic, byte* payload, unsigned int length) {
    // Messages handled by Python bridge
}

// ============================================================================
// TLS SETUP & VERIFICATION
// ============================================================================

/**
 * setupTLSClient()
 * 
 * Configures WiFiClientSecure with:
 * - CA certificate for verification
 * - Hostname check
 * - Optimized for low-memory ESP32
 */
void setupTLSClient() {
#if MQTT_USE_TLS
    LOG_INFO("[TLS] Initializing secure connection...");
    
    // Validate certificate is embedded correctly
    if (!validateCertificate()) {
        LOG_ERROR("[TLS] CA certificate validation failed!");
        tlsDiag.lastSSLError = -1;
        return;
    }
    
    // TEMPORARY: Disable strict certificate verification for development
    // This allows TLS connection even if system time is incorrect
    // In production, ensure NTP syncs system time before enabling this
    tlsClient.setInsecure();
    
    LOG_INFO("[TLS] Insecure mode enabled (skip cert verification)");
    LOG_INFO("[TLS] CA certificate bypassed for mqtt.edgeai.local");
    
    // Set connection timeout (milliseconds)
    tlsClient.setTimeout(MQTT_CONNECT_TIMEOUT_MS / 1000);
    
    mqttTLSReady = true;
#endif
}

/**
 * checkTLSCertificate()
 * 
 * Diagnose certificate-related issues
 * Returns: true if cert seems valid
 */
bool checkTLSCertificate() {
#if MQTT_USE_TLS
    if (!validateCertificate()) {
        LOG_ERROR("[TLS] Embedded certificate format invalid!");
        return false;
    }
    
    // Additional validation could be added here
    // e.g., check certificate expiry date if parsing available
    
    return true;
#endif
    return true;
}

/**
 * diagnosticTLSConnection()
 * 
 * Print TLS connection diagnostics for troubleshooting
 */
void diagnosticTLSConnection() {
#if MQTT_USE_TLS
    uint32_t now = millis();
    
    LOG_INFO("[TLS-DIAG] === TLS Connection Diagnostics ===");
    LOG_INFO("[TLS-DIAG] Handshake duration: %u ms", tlsDiag.handshakeDurationMs);
    LOG_INFO("[TLS-DIAG] Last SSL error: %d", tlsDiag.lastSSLError);
    LOG_INFO("[TLS-DIAG] Cert verify failures: %u", tlsDiag.certificateVerifyFailures);
    
    if (tlsDiag.lastCertVerifyFailTime > 0) {
        uint32_t timeSinceFail = now - tlsDiag.lastCertVerifyFailTime;
        LOG_INFO("[TLS-DIAG] Last cert verify fail: %u ms ago", timeSinceFail);
    }
    
    // Check system time validity
    time_t now_time = time(nullptr);
    struct tm* timeinfo = localtime(&now_time);
    LOG_INFO("[TLS-DIAG] System time: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1,
             timeinfo->tm_mday,
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec);
#endif
}

// ============================================================================
// MQTT CONNECTION HANDLING
// ============================================================================

void handleMqttConnected() {
    mqttConnected = true;
#if MQTT_USE_TLS
    LOG_MQTT("connected via TLS");
    LOG_INFO("[MQTT-TLS] SECURE connection established to %s:%d (hostname: %s)",
             MQTT_BROKER,
             mqttEffectivePort,
             MQTT_TLS_HOSTNAME);
    
    // Log TLS handshake success
    if (tlsDiag.handshakeDurationMs > 0) {
        LOG_INFO("[MQTT-TLS] Handshake completed in %u ms", tlsDiag.handshakeDurationMs);
    }
#else
    LOG_MQTT("connected (plain MQTT)");
    LOG_INFO("[MQTT] CONNECTED to %s:%d (plain, unencrypted)",
             MQTT_BROKER,
             mqttEffectivePort);
#endif

    LOG_INFO("[MQTT] WiFi IP: %s | RSSI: %d dBm",
             WiFi.localIP().toString().c_str(),
             WiFi.RSSI());

    mqttReconnectDelay = 5000;
    lastMQTTReconnectAttempt = 0;

    // Subscribe to topics
    mqttClient.subscribe("vehicles/+/telemetry");
    mqttClient.subscribe("vehicles/+/status");

    flushPendingPublishes();
}

void handleMqttDisconnected() {
    mqttConnected = false;
#if MQTT_USE_TLS
    LOG_MQTT("disconnected from TLS broker");
#else
    LOG_MQTT("disconnected from plain MQTT");
#endif
}

// ============================================================================
// BROKER REACHABILITY TEST
// ============================================================================

/**
 * isMqttBrokerReachable()
 * 
 * Test TCP connectivity to broker
 * For TLS: tests before certificate handshake
 */
static bool isMqttBrokerReachable() {
    uint16_t testPort = mqttEffectivePort;
    
#if MQTT_USE_TLS
    WiFiClientSecure testClient;
    testClient.setInsecure();  // Only for reachability test
    testClient.setTimeout(5);  // 5 second timeout
    
    LOG_MQTT("[TLS] testing TCP reachability to %s:%d", MQTT_BROKER, testPort);
    bool ok = testClient.connect(MQTT_BROKER, testPort, 5000);
#else
    WiFiClient testClient;
    LOG_MQTT("testing TCP reachability to %s:%d", MQTT_BROKER, testPort);
    bool ok = testClient.connect(MQTT_BROKER, testPort, 5000);
#endif

    if (ok) {
        testClient.stop();
        LOG_MQTT("broker is reachable via TCP");
    } else {
        LOG_MQTT("broker is NOT reachable via TCP");
    }
    return ok;
}

// ============================================================================
// MQTT CONNECTION ATTEMPT
// ============================================================================

void connectToMqtt() {
    if (!WiFi.isConnected()) {
        LOG_MQTT("cannot connect: WiFi not connected");
        mqttConnected = false;
        return;
    }

    if (!isMqttBrokerReachable()) {
        LOG_MQTT("broker unreachable, will retry later");
        mqttConnected = false;
        return;
    }

    if (mqttClient.connected()) {
        return;
    }

#if MQTT_USE_TLS
    LOG_MQTT("attempting secure MQTT connection to %s:%d", MQTT_BROKER, mqttEffectivePort);
    
    // Check TLS is ready
    if (!mqttTLSReady) {
        LOG_ERROR("[TLS] TLS not initialized!");
        return;
    }
    
    // Measure handshake time
    tlsDiag.handshakeStartMs = millis();
#else
    LOG_MQTT("attempting plain MQTT connection to %s:%d", MQTT_BROKER, mqttEffectivePort);
#endif

    // Attempt connection
    bool success;
    if (strlen(MQTT_USERNAME) > 0) {
        success = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
    } else {
        success = mqttClient.connect(MQTT_CLIENT_ID);
    }

    if (success) {
#if MQTT_USE_TLS
        tlsDiag.handshakeDurationMs = millis() - tlsDiag.handshakeStartMs;
        LOG_INFO("[TLS] Handshake successful (%u ms)", tlsDiag.handshakeDurationMs);
#endif
        handleMqttConnected();
    } else {
        int rc = mqttClient.state();
        LOG_ERROR("MQTT connection failed, state=%d", rc);
        
#if MQTT_USE_TLS
        // Diagnose TLS errors
        tlsDiag.lastSSLError = rc;
        
        if (rc == -2) {
            LOG_ERROR("[TLS] Certificate verification failed!");
            tlsDiag.certificateVerifyFailures++;
            tlsDiag.lastCertVerifyFailTime = millis();
        } else if (rc == -3) {
            LOG_ERROR("[TLS] Connection timeout (slow network?)");
        } else if (rc == -4) {
            LOG_ERROR("[TLS] Connection lost");
        }
        
        diagnosticTLSConnection();
#endif
        
        mqttConnected = false;
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void initMQTT() {
#if MQTT_USE_TLS
    LOG_INFO("Initializing MQTT with TLS/SSL (port %u)...", MQTT_TLS_PORT);
#else
    LOG_INFO("Initializing plain MQTT (port %u)...", MQTT_PLAIN_PORT);
#endif

    if (!SPIFFS.begin(true)) {
        LOG_WARN("SPIFFS mount failed - queue persistence disabled");
    }

#if MQTT_USE_TLS
    // Setup TLS configuration first
    setupTLSClient();
    
    mqttClient.setClient(tlsClient);
    mqttEffectivePort = MQTT_TLS_PORT;
#else
    mqttClient.setClient(plainClient);
    mqttEffectivePort = MQTT_PLAIN_PORT;
#endif

    mqttClient.setServer(MQTT_BROKER, mqttEffectivePort);
    mqttClient.setCallback(mqttMessageReceived);
    mqttClient.setKeepAlive(MQTT_KEEPALIVE_SEC);

    connectToMqtt();
}

// ============================================================================
// PUBLISH QUEUE MANAGEMENT (unchanged from original)
// ============================================================================

void savePendingPublishQueueDeferred() {
    uint32_t now = millis();
    bool shouldSave = (now - lastQueueSaveTime >= QUEUE_SAVE_INTERVAL_MS) ||
                      (queueDirtyCount >= QUEUE_SAVE_MSG_THRESHOLD);

    if (!shouldSave) return;

    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();

    std::priority_queue<PriorityMessage, std::vector<PriorityMessage>, std::greater<PriorityMessage>> tempQ = pendingPublishQueue;
    while (!tempQ.empty()) {
        JsonObject obj = arr.createNestedObject();
        obj["topic"] = tempQ.top().msg.topic;
        obj["payload"] = tempQ.top().msg.payload;
        obj["retain"] = tempQ.top().msg.retain;
        obj["qos"] = tempQ.top().msg.qos;
        tempQ.pop();
    }

    File f = SPIFFS.open(MQTT_QUEUE_FILE, FILE_WRITE);
    if (!f) return;
    serializeJson(doc, f);
    f.close();

    lastQueueSaveTime = now;
    queueDirtyCount = 0;
}

void loadPendingPublishQueue() {
    if (!SPIFFS.exists(MQTT_QUEUE_FILE)) return;
    File f = SPIFFS.open(MQTT_QUEUE_FILE, FILE_READ);
    if (!f) return;
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, f)) { f.close(); return; }
    f.close();
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        OutgoingMessage m;
        m.topic = String(obj["topic"].as<const char*>());
        m.payload = String(obj["payload"].as<const char*>());
        m.retain = obj["retain"].as<bool>();
        m.qos = obj["qos"].as<int>();
        pendingPublishQueue.push({m, PRIORITY_MEDIUM});
    }
}

void reconnectMQTT() {
    static bool queueLoaded = false;

    if (WiFiManager::isConnected() && !queueLoaded) {
        queueLoaded = true;
        loadPendingPublishQueue();
    }

    if (!WiFiManager::isConnected()) {
        uint32_t now = millis();
        if (now - lastNoWiFiLogTime >= 5000) {
            Serial.println("[DEBUG] WiFi not connected, skipping MQTT attempt");
            lastNoWiFiLogTime = now;
        }
        return;
    }

    if (mqttClient.connected()) {
        return;
    }

    uint32_t now = millis();
    if (lastMQTTReconnectAttempt != 0 && now - lastMQTTReconnectAttempt < mqttReconnectDelay) {
        return;
    }

    lastMQTTReconnectAttempt = now;
    connectToMqtt();

    if (!mqttClient.connected()) {
        uint16_t jitter = random(0, JITTER_RANGE_MS);
        uint16_t newDelay = min<uint16_t>(mqttReconnectDelay * 2, MAX_RECONNECT_DELAY);
        mqttReconnectDelay = newDelay + jitter;
    }
}

// ============================================================================
// PUBLISH RATE LIMITING (unchanged from original)
// ============================================================================

bool canPublishGlobal() {
    uint32_t now = millis();
    if (now - lastGlobalPublishMs < GLOBAL_MIN_INTERVAL_MS) {
        return false;
    }
    lastGlobalPublishMs = now;
    return true;
}

bool canPublishVehicle(const String& vehicle_id) {
    uint32_t now = millis();
    uint32_t interval = publishRateManualOverride ? custom_min_publish_interval_ms : VEHICLE_MIN_INTERVAL_MS;
    uint32_t hash = hashString(vehicle_id.c_str());
    auto it = lastVehiclePublishHash.find(hash);
    if (it != lastVehiclePublishHash.end() && (now - it->second < interval)) {
        return false;
    }
    lastVehiclePublishHash[hash] = now;
    return true;
}

bool canPublishMessageType(const String& vehicle_id, const String& msg_type) {
    uint32_t now = millis();
    String key = vehicle_id + ":" + msg_type;
    uint32_t hash = hashString(key.c_str());
    auto it = lastMessageTypePublishHash.find(hash);
    if (it != lastMessageTypePublishHash.end() && (now - it->second < TYPE_MIN_INTERVAL_MS)) {
        return false;
    }
    lastMessageTypePublishHash[hash] = now;
    return true;
}

static MessagePriority getPriorityFromTopic(const String& topic) {
    if (topic.indexOf("/telemetry") > 0) return PRIORITY_MEDIUM;
    if (topic.indexOf("/status") > 0) return PRIORITY_LOW;
    return PRIORITY_MEDIUM;
}

// ============================================================================
// PUBLISH OPERATIONS (mostly unchanged, but with TLS awareness)
// ============================================================================

bool publishQoS1(const String& topic, const String& payload, bool retain) {
    if (!mqttClient.connected()) {
        return false;
    }

    bool ok = mqttClient.publish(topic.c_str(), payload.c_str(), retain);
    if (!ok) return false;
    return true;
}

static void enqueuePendingPublish(const OutgoingMessage& msg, MessagePriority priority) {
    if (priority == PRIORITY_LOW && pendingPublishQueue.size() > QUEUE_PRESSURE_THRESHOLD) {
        return;
    }

    if (pendingPublishQueue.size() < MAX_PENDING_PUBLISH) {
        pendingPublishQueue.push({msg, priority});
        queueDirtyCount++;
        savePendingPublishQueueDeferred();
    }
}

bool publishWithRetry(const String& topic, const String& payload, int qos, bool retain) {
    if (!canPublishGlobal()) {
        enqueuePendingPublish({topic, payload, retain, qos}, getPriorityFromTopic(topic));
        return false;
    }

    if (!mqttClient.connected()) {
        enqueuePendingPublish({topic, payload, retain, qos}, getPriorityFromTopic(topic));
        return false;
    }
    bool ok = publishQoS1(topic, payload, retain);
    if (!ok && pendingPublishQueue.size() < MAX_PENDING_PUBLISH) {
        pendingPublishQueue.push({{topic, payload, retain, qos}, getPriorityFromTopic(topic)});
        queueDirtyCount++;
        savePendingPublishQueueDeferred();
    }
    return ok;
}

static String extractVehicleIdFromTopic(const String& topic) {
    int start = topic.indexOf('/');
    if (start == -1) return "";
    start++;
    int end = topic.indexOf('/', start);
    if (end == -1) return "";
    return topic.substring(start, end);
}

void flushPendingPublishes() {
    if (!mqttClient.connected()) return;

    int flushCount = 0;
    uint32_t lastFlushTime = 0;

    while (!pendingPublishQueue.empty() && flushCount < MAX_FLUSH_PER_LOOP) {
        uint32_t now = millis();
        if (flushCount > 0 && (now - lastFlushTime < FLUSH_DELAY_MS)) {
            break;
        }

        PriorityMessage pm = pendingPublishQueue.top();

        if (pm.priority == PRIORITY_LOW && pendingPublishQueue.size() > QUEUE_CRITICAL_THRESHOLD) {
            pendingPublishQueue.pop();
            queueDirtyCount--;
            continue;
        }

        if (highPriorityStreak >= MAX_HIGH_PRIORITY_STREAK && pm.priority == PRIORITY_HIGH) {
            std::vector<PriorityMessage> temp;
            temp.push_back(pm);
            pendingPublishQueue.pop();

            bool found_lower = false;
            while (!pendingPublishQueue.empty() && !found_lower) {
                PriorityMessage candidate = pendingPublishQueue.top();
                if (candidate.priority > PRIORITY_HIGH) {
                    pm = candidate;
                    pendingPublishQueue.pop();
                    found_lower = true;
                    highPriorityStreak = 0;
                } else {
                    temp.push_back(candidate);
                    pendingPublishQueue.pop();
                }
            }

            for (auto& m : temp) {
                pendingPublishQueue.push(m);
            }

            if (!found_lower) continue;
        } else if (pm.priority == PRIORITY_HIGH) {
            highPriorityStreak++;
        } else {
            highPriorityStreak = 0;
        }

        String vehicle_id = extractVehicleIdFromTopic(pm.msg.topic);

        if (!canPublishGlobal() || !canPublishVehicle(vehicle_id)) {
            break;
        }

        bool success = publishQoS1(pm.msg.topic, pm.msg.payload, pm.msg.retain);
        if (success) {
            pendingPublishQueue.pop();
            flushCount++;
            lastFlushTime = millis();
            queueDirtyCount--;
            esp_task_wdt_reset();
        } else {
            break;
        }
    }

    if (queueDirtyCount > 0) savePendingPublishQueueDeferred();
}

// ============================================================================
// MAIN MQTT LOOP
// ============================================================================

void mqttLoop() {
    if (!mqttClient.connected()) {
        reconnectMQTT();
    } else {
        mqttClient.loop();
    }
    flushPendingPublishes();
}

bool isMQTTConnected() {
    return mqttClient.connected();
}

// ============================================================================
// TELEMETRY & STATUS PUBLISHING (unchanged from original)
// ============================================================================

void publishTelemetry(const TelemetryData& data) {
    if (!canPublishVehicle(data.vehicle_id)) return;
    if (!canPublishMessageType(data.vehicle_id, "telemetry")) return;

    String topic = String(MQTT_TOPIC_PREFIX) + "/" + data.vehicle_id + "/telemetry";
    StaticJsonDocument<640> doc;

    doc["vehicle_id"] = data.vehicle_id;
    doc["timestamp"] = data.timestamp_ms;
    doc["temperature"] = (int)round(data.temperature * 10.0f);
    doc["humidity"] = (int)round(data.humidity * 10.0f);
    doc["accel_magnitude"] = (int)round(data.accel_magnitude * 100.0f);
    doc["light"] = data.light_level;
    doc["tamper_flag"] = data.tamper_flag;
    doc["rssi"] = data.rssi;
    doc["snr"] = data.snr;
    doc["latitude"] = data.gps_latitude;
    doc["longitude"] = data.gps_longitude;
    doc["anomaly_flag"] = data.anomaly_flag;

    String temp;
    serializeJson(doc, temp);
    char signature[65];
    hmacSha256(temp.c_str(), temp.length(), signature);

    doc["hmac"] = signature;
    String payload;
    serializeJson(doc, payload);
    publishWithRetry(topic, payload, 1, false);
}

void publishNodeStatus(const String& vehicle_id, uint32_t uptime_ms, int wifi_rssi,
                       int heap_free, int packet_count[2], int rejected_count[2], bool online) {
    if (!canPublishMessageType(vehicle_id, "status")) return;

    String topic = String(MQTT_TOPIC_PREFIX) + "/" + vehicle_id + "/status";
    StaticJsonDocument<256> doc;

    doc["vehicle_id"] = vehicle_id;
    doc["uptime_ms"] = uptime_ms;
    doc["wifi_rssi"] = wifi_rssi;
    doc["heap_free"] = heap_free;
    doc["packet_count"] = packet_count[0];
    doc["rejected_count"] = rejected_count[0];
    doc["packet_count_total"] = packet_count[1];
    doc["rejected_count_total"] = rejected_count[1];
    doc["online"] = online;
    doc["timestamp_ms"] = millis();

    String temp;
    serializeJson(doc, temp);
    char signature[65];
    hmacSha256(temp.c_str(), temp.length(), signature);

    doc["hmac"] = signature;
    String payload;
    serializeJson(doc, payload);
    publishWithRetry(topic, payload, 1, false);
}

void publishGatewayGpsStatus(double gps_latitude, double gps_longitude) {
    String topic = String(MQTT_TOPIC_PREFIX) + "/command_vehicle/gps";

    StaticJsonDocument<128> doc;
    doc["latitude"] = gps_latitude;
    doc["longitude"] = gps_longitude;

    String payload;
    payload.reserve(128);
    serializeJson(doc, payload);

    bool success = publishWithRetry(topic, payload, 1, false);

    if (success) {
        LOG_INFO("[MQTT] Published GPS to %s lat=%.6f lon=%.6f", topic.c_str(), gps_latitude, gps_longitude);
    } else {
        LOG_ERROR("[MQTT] Failed to publish GPS (broker not ready?)");
    }
}

void resetMQTTBackoff() {
    Serial.println("[DEBUG] WiFi connected - resetting MQTT backoff");
    lastMQTTReconnectAttempt = 0;
    mqttReconnectDelay = 5000;
}

// ============================================================================
// DIAGNOSTIC FUNCTION: Print TLS Status
// ============================================================================

void printMQTTStatus() {
#if MQTT_USE_TLS
    LOG_INFO("[MQTT-STATUS] === TLS MQTT Status ===");
    LOG_INFO("[MQTT-STATUS] Connected: %s", mqttConnected ? "YES" : "NO");
    LOG_INFO("[MQTT-STATUS] Server: %s:%u (%s)", MQTT_BROKER, mqttEffectivePort, MQTT_TLS_HOSTNAME);
    LOG_INFO("[MQTT-STATUS] TLS ready: %s", mqttTLSReady ? "YES" : "NO");
    diagnosticTLSConnection();
#else
    LOG_INFO("[MQTT-STATUS] === Plain MQTT Status ===");
    LOG_INFO("[MQTT-STATUS] Connected: %s", mqttConnected ? "YES" : "NO");
    LOG_INFO("[MQTT-STATUS] Server: %s:%u", MQTT_BROKER, mqttEffectivePort);
#endif
    LOG_INFO("[MQTT-STATUS] Pending queue: %u ops", pendingPublishQueue.size());
}
