#include "mqtt_client.h"
#include "config.h"
#include "crypto_utils.h"
#include "wifi_manager.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <esp_task_wdt.h>
#include <deque>
#include <queue>
#include <unordered_map>

WiFiClient plainClient;
PubSubClient mqttClient(plainClient);

uint16_t mqttEffectivePort = MQTT_PORT;
bool mqttConnected = false;

uint32_t lastMQTTReconnectAttempt = 0;
uint16_t mqttReconnectDelay = 5000; 
const uint16_t MAX_RECONNECT_DELAY = 60000;
const uint16_t MIN_RECONNECT_DELAY = 1000;  
uint32_t lastNoWiFiLogTime = 0;

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
const uint32_t FLUSH_DELAY_MS = 20;  // 20ms delay between flushes

// Batched SPIFFS saves
uint32_t lastQueueSaveTime = 0;
const uint32_t QUEUE_SAVE_INTERVAL_MS = 5000;
uint32_t queueDirtyCount = 0;
const int QUEUE_SAVE_MSG_THRESHOLD = 10;

// Priority queue fairness (prevent starvation)
int highPriorityStreak = 0;
const int MAX_HIGH_PRIORITY_STREAK = 3;

// Backpressure control
const size_t QUEUE_PRESSURE_THRESHOLD = 150;
const size_t QUEUE_CRITICAL_THRESHOLD = 190;

// Multi-layer rate limiting
const uint32_t VEHICLE_MIN_INTERVAL_MS = 1000;
const uint32_t TYPE_MIN_INTERVAL_MS = 200;

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

// FS queue file
const char *MQTT_QUEUE_FILE = "/mqtt_pub_queue.json";


void mqttMessageReceived(char* topic, byte* payload, unsigned int length) {
    Serial.printf("[MQTT] Received message on topic %s (len=%u), ignored\n", topic, length);
}


void handleMqttConnected() {
    mqttConnected = true;
    LOG_MQTT("connected (PubSubClient)");
    LOG_INFO("[MQTT] CONNECTED to %s:%d | WiFi IP: %s | RSSI: %d dBm",
             MQTT_BROKER,
             mqttEffectivePort,
             WiFi.localIP().toString().c_str(),
             WiFi.RSSI());

    mqttReconnectDelay = 5000;
    lastMQTTReconnectAttempt = 0;

    mqttClient.subscribe("vehicles/+/telemetry");
    mqttClient.subscribe("vehicles/+/status");

    flushPendingPublishes();
}

void handleMqttDisconnected() {
    mqttConnected = false;
    LOG_MQTT("disconnected");
}

static bool isMqttBrokerReachable() {
    WiFiClient tester;
    LOG_MQTT("testing TCP reachability to %s:%d", MQTT_BROKER, mqttEffectivePort);
    bool ok = tester.connect(MQTT_BROKER, mqttEffectivePort, 2000);
    if (ok) {
        tester.stop();
        LOG_MQTT("broker is reachable via TCP");
    } else {
        LOG_MQTT("broker is NOT reachable via TCP");
    }
    return ok;
}

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

    LOG_MQTT("attempt connect to MQTT %s:%d", MQTT_BROKER, mqttEffectivePort);

    bool success;
    if (strlen(MQTT_USERNAME) > 0) {
        success = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
    } else {
        success = mqttClient.connect(MQTT_CLIENT_ID);
    }

    if (success) {
        handleMqttConnected();
    } else {
        LOG_MQTT("MQTT connect failed rc=%d", mqttClient.state());
        mqttConnected = false;
    }
}

void onWiFiEvent(WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        resetMQTTBackoff();
        connectToMqtt();
    }
}


void initMQTT() {
    LOG_INFO("Initializing MQTT (PubSubClient)...");

    if (!SPIFFS.begin(true)) {
        LOG_WARN("SPIFFS mount failed");
    }

    mqttClient.setClient(plainClient);
    mqttEffectivePort = 1883;

    mqttClient.setServer(MQTT_BROKER, mqttEffectivePort);
    mqttClient.setCallback(mqttMessageReceived);

    connectToMqtt();
}

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

bool publishQoS1(const String& topic, const String& payload, bool retain) {
    if (!mqttClient.connected()) {
        return false;
    }

    bool ok = mqttClient.publish(topic.c_str(), payload.c_str(), retain);
    if (!ok) return false;
    return true;
}

static void enqueuePendingPublish(const OutgoingMessage& msg, MessagePriority priority) {
    // Backpressure: reject LOW priority if queue is filling
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
        
        // Backpressure: drop LOW priority if queue critical
        if (pm.priority == PRIORITY_LOW && pendingPublishQueue.size() > QUEUE_CRITICAL_THRESHOLD) {
            pendingPublishQueue.pop();
            queueDirtyCount--;
            continue;
        }
        
        // Fairness: force non-HIGH if HIGH streak too long
        if (highPriorityStreak >= MAX_HIGH_PRIORITY_STREAK && pm.priority == PRIORITY_HIGH) {
            // Skip this HIGH, try to find MEDIUM/LOW
            std::vector<PriorityMessage> temp;
            temp.push_back(pm);
            pendingPublishQueue.pop();
            
            bool found_lower = false;
            while (!pendingPublishQueue.empty() && !found_lower) {
                PriorityMessage candidate = pendingPublishQueue.top();
                if (candidate.priority < PRIORITY_HIGH) {
                    pm = candidate;
                    pendingPublishQueue.pop();
                    found_lower = true;
                    highPriorityStreak = 0;
                } else {
                    temp.push_back(candidate);
                    pendingPublishQueue.pop();
                }
            }
            
            // Restore skipped messages
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
            esp_task_wdt_reset();  // Prevent watchdog during flush loop
        } else {
            break;
        }
    }
    
    if (queueDirtyCount > 0) savePendingPublishQueueDeferred();
}

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
    doc["nn_anomaly_score"] = (int)round(data.nn_anomaly_score * 100.0f);
    doc["nn_anomaly_state"] = data.nn_anomaly_state;
    
    // FIX: Calculate HMAC BEFORE adding hmac field to JSON
    String temp;
    serializeJson(doc, temp);
    char signature[65];
    hmacSha256(temp.c_str(), temp.length(), signature);
    
    // Add HMAC field THEN serialize full payload
    // This ensures Python bridge calculates HMAC on same JSON structure
    doc["hmac"] = signature;
    String payload;
    serializeJson(doc, payload);
    publishWithRetry(topic, payload, 1, false);
}

void publishNodeStatus(const String& vehicle_id, uint32_t uptime_ms, int wifi_rssi,
                       int heap_free, int packet_count[2], int rejected_count[2], bool online) {
    if (!canPublishVehicle(vehicle_id)) return;
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
    
    // FIX: Calculate HMAC BEFORE adding hmac field
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
        Serial.printf("[MQTT-COMMAND] GPS lat=%.6f lon=%.6f\n", gps_latitude, gps_longitude);
    }
}

void resetMQTTBackoff() {
    Serial.println("[DEBUG] WiFi connected - resetting MQTT backoff delay");
    lastMQTTReconnectAttempt = 0;  
    mqttReconnectDelay = 5000;    
}
