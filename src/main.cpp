#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "esp_log.h"

#include <algorithm>
#include <numeric>
#include <map>
#include <unordered_map>
#include <string>

#include "tensorflow/lite/micro/micro_error_reporter.h"

#include "config.h"
#include "serial_reader.h"
#include "json_parser.h"
#include "crypto_utils.h"
#include "gps.h"
#include "mqtt_client.h"
#include "firestore_client.h"
#include "system_state.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "websocket_server.h"
#include "config_storage.h"
#include "status_publisher.h"
#include "tinyml_model.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

constexpr int TENSOR_ARENA_SIZE = 20 * 1024;
static uint8_t tensor_arena[TENSOR_ARENA_SIZE];

static tflite::MicroInterpreter* interpreter = nullptr;
static TfLiteTensor* input = nullptr;
static TfLiteTensor* output = nullptr;

static bool nn_initialized = false;
static tflite::MicroErrorReporter micro_error_reporter;

enum VehicleNodeID {
    NODE_UNKNOWN = 0,
    NODE_TRANSPORT_1 = 1,
    NODE_TRANSPORT_2 = 2,
};

VehicleStatsFixed gVehicleStats;

constexpr int WINDOW_SIZE = 12;      // 12 samples per window
constexpr int CONSISTENCY_WINDOW = 3; // Check last 3 windows for consensus
constexpr float ANOMALY_THRESHOLD = 0.92f;  // FIX: Raise from 0.85 -> 0.98 (avoid false positives from distribution shift)
constexpr float NORMAL_THRESHOLD = 0.7f;   // FIX: Raise from 0.65 -> 0.70 (hysteresis range)
constexpr int CONSENSUS_COUNT = 2;   // FIX: Require 2/3 windows to confirm anomaly (stricter consensus)

struct SensorSample {
    float temp;
    float humidity;
    float accel;
    uint32_t timestamp;
};

struct WindowFeatures {
    // Statistics over window
    float mean_temp, mean_humi, mean_accel;
    float std_temp, std_humi, std_accel;
    
    // Rate of change
    float delta_temp, delta_humi, delta_accel;
    
    // Acceleration characteristics
    float max_accel_spike;
    float accel_energy;      // Variance of accel in window
};

class SlicingWindowDetector {
private:
    SensorSample window_buffer[WINDOW_SIZE];
    int buffer_index = 0;
    bool buffer_full = false;
    
    float window_scores[CONSISTENCY_WINDOW] = {0.0f};
    int score_index = 0;
    bool warmup_complete = false;  // FIX: Track warmup properly
    int score_count = 0;           // FIX: Count total scores
    
public:
    // ===== Constructor =====
    SlicingWindowDetector() {
        reset();
    }

    // ===== Reset detector state =====
    void reset() {

        // Clear scores
        for (int i = 0; i < CONSISTENCY_WINDOW; i++) {
            window_scores[i] = 0.0f;
        }

        // Clear sensor buffer
        for (int i = 0; i < WINDOW_SIZE; i++) {
            window_buffer[i] = {0,0,0,0};
        }

        buffer_index = 0;
        buffer_full = false;

        score_index = 0;
        score_count = 0;

        warmup_complete = false;
    }

    void addSample(float t, float h, float a) {
        window_buffer[buffer_index] = {t, h, a, (uint32_t)millis()};
        buffer_index = (buffer_index + 1) % WINDOW_SIZE;
        
        if (buffer_index == 0) {
            buffer_full = true;
        }
    }
    
    WindowFeatures extractWindowFeatures() {
        WindowFeatures feat = {};
        
        if (!buffer_full) return feat;
        
        // Calculate statistics
        float sum_t = 0, sum_h = 0, sum_a = 0;
        float min_a = 999, max_a = -999;
        
        for (int i = 0; i < WINDOW_SIZE; i++) {
            sum_t += window_buffer[i].temp;
            sum_h += window_buffer[i].humidity;
            sum_a += window_buffer[i].accel;
            
            max_a = max(max_a, window_buffer[i].accel);
            min_a = min(min_a, window_buffer[i].accel);
        }
        
        feat.mean_temp = sum_t / WINDOW_SIZE;
        feat.mean_humi = sum_h / WINDOW_SIZE;
        feat.mean_accel = sum_a / WINDOW_SIZE;
        
        // Calculate std dev
        float var_t = 0, var_h = 0, var_a = 0;
        for (int i = 0; i < WINDOW_SIZE; i++) {
            var_t += pow(window_buffer[i].temp - feat.mean_temp, 2);
            var_h += pow(window_buffer[i].humidity - feat.mean_humi, 2);
            var_a += pow(window_buffer[i].accel - feat.mean_accel, 2);
        }
        
        feat.std_temp = sqrt(var_t / WINDOW_SIZE);
        feat.std_humi = sqrt(var_h / WINDOW_SIZE);
        feat.std_accel = sqrt(var_a / WINDOW_SIZE);
        
        int idx_start = (buffer_index + 1) % WINDOW_SIZE;  // Next would have overwritten this first
        int idx_end = (buffer_index - 1 + WINDOW_SIZE) % WINDOW_SIZE;   // Most recently added
        
        feat.delta_temp = window_buffer[idx_end].temp - window_buffer[idx_start].temp;
        feat.delta_humi = window_buffer[idx_end].humidity - window_buffer[idx_start].humidity;
        feat.delta_accel = window_buffer[idx_end].accel - window_buffer[idx_start].accel;
        
        // Acceleration spike
        feat.max_accel_spike = max(fabs(max_a), fabs(min_a));
        feat.accel_energy = feat.std_accel;  // Energy as variance
        
        return feat;
    }
    
    void updateWindowScore(float score) {
        window_scores[score_index] = score;
        score_index = (score_index + 1) % CONSISTENCY_WINDOW;
        score_count++;  // FIX: Track total count
        
        // FIX: Exit warmup after enough scores collected
        if (!warmup_complete && score_count >= CONSISTENCY_WINDOW) {
            warmup_complete = true;
        }
    }
    
    bool isAnomalyConfirmed() {
        if (!buffer_full) return false;
        
        int anomaly_count = 0;
        for (int i = 0; i < CONSISTENCY_WINDOW; i++) {
            if (window_scores[i] > ANOMALY_THRESHOLD) {
                anomaly_count++;
            }
        }
        
        // Need consensus: k/N windows confirm anomaly
        return anomaly_count >= CONSENSUS_COUNT;
    }
    
    bool isNormalConfirmed() {
        if (!buffer_full) return false;
        
        int normal_count = 0;
        for (int i = 0; i < CONSISTENCY_WINDOW; i++) {
            if (window_scores[i] < NORMAL_THRESHOLD) {
                normal_count++;
            }
        }
        
        // Most windows below threshold = normal
        return normal_count >= (CONSISTENCY_WINDOW - 1);
    }
    
    // ===== NEW: Check if detector is ready =====
    bool isReady() {
        // FIX: Wait until buffer is full AND warmup complete
        return buffer_full && warmup_complete;
    }
};

// Per-node anomaly detectors to isolate multi-node interference
std::map<int, SlicingWindowDetector> nodeDetectors;
std::map<int, bool> nodeAnomalyState;
std::map<int, bool> nodeLastState;

struct StandardScalerESP32 {

    float t_mean = 28.8260f;
    float h_mean = 63.2910f;
    float a_mean = 0.7697f;

    float t_std  = 5.7152f;
    float h_std  = 19.7242f;
    float a_std  = 0.9913f;

    inline float norm(float x, float mean, float std) {
        if (std == 0) return 0.0f;
        return (x - mean) / std;
    }
};

static StandardScalerESP32 scalerESP;


static bool isBase64Payload(const String& payload) {
    String trimmed = payload;
    trimmed.trim();
    if (trimmed.length() == 0 || (trimmed.length() % 4) != 0) {
        return false;
    }
    for (size_t i = 0; i < trimmed.length(); ++i) {
        char c = trimmed[i];
        if (!(isalnum(c) || c == '+' || c == '/' || c == '=')) {
            return false;
        }
    }
    return true;
}

bool verifySignedJson(const String &signedJson, String &outPlainJson) {
    DynamicJsonDocument doc(512);
    auto err = deserializeJson(doc, signedJson);
    if (err) {
        LOG_ERROR("[SEC] JSON parse failed in verifySignedJson: %s", err.c_str());
        return false;
    }

    if (!doc.containsKey("sig")) {
        LOG_ERROR("[SEC] Missing sig field in signed JSON");
        return false;
    }

    String signature = doc["sig"].as<String>();
    doc.remove("sig");

    String payload;
    payload.reserve(128);
    payload = String("{\"v\":\"") + doc["v"].as<const char*>() +
              String("\",\"ts\":") + String((uint32_t)doc["ts"].as<uint32_t>()) +
              String(",\"t\":") + String(doc["t"].as<float>(), 1) +
              String(",\"h\":") + String(doc["h"].as<float>(), 1) +
              String(",\"a\":") + String(doc["a"].as<float>(), 2) +
              String(",\"l\":") + String((uint32_t)doc["l"].as<uint32_t>()) +
              String(",\"x\":") + String((int)doc["x"].as<int>()) +
              String(",\"la\":") + String(doc["la"].as<double>(), 5) +
              String(",\"lo\":") + String(doc["lo"].as<double>(), 5) +
              String("}");

    char expectedSig[65];
    if (!hmacSha256(payload.c_str(), payload.length(), expectedSig)) {
        LOG_ERROR("[SEC] HMAC generation failed");
        return false;
    }
    if (strcmp(expectedSig, signature.c_str()) != 0) {
        LOG_ERROR("[SEC] HMAC mismatch: expected=%s received=%s", expectedSig, signature.c_str());
        return false;
    }

    outPlainJson = payload;
    return true;
}

void publishGatewayGpsStatusIfNeeded();
void publishNodeStatusPing();
void handleSerialData();
void blinkLED();

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(1000); 
    
    esp_log_level_set("wifi", ESP_LOG_NONE);
    esp_log_level_set("WiFiGeneric", ESP_LOG_NONE);
    esp_log_level_set("wifi_init", ESP_LOG_NONE);
    esp_log_level_set("system_api", ESP_LOG_NONE);
    esp_log_level_set("esp_netif", ESP_LOG_NONE);
    
    LOG_INFO("RX_ESP32_Firestore starting - Production WiFi Config System");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    WiFi.mode(WIFI_OFF);       
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    delay(500); 
    
    // ========== POWER MANAGEMENT ==========
    // Reduce CPU frequency to 80 MHz to save power (from 240 MHz default)
    setCpuFrequencyMhz(80);
    LOG_INFO("CPU frequency reduced to 80 MHz for power savings");
    
    WiFiManager::init();
    ConfigStorage::debugDump();
    
    // Force update MQTT config from config.h to override old stored values
    ConfigStorage::setMqttConfig(MQTT_BROKER, String(MQTT_PORT));
    LOG_INFO("[SETUP] MQTT config updated from config.h: %s:%d", MQTT_BROKER, MQTT_PORT);
    
    WebServer::init(WIFI_AP_PORT);
    
    // WebSocket initialization deferred until WiFi is ready (in loop)
    
    initSerialReader();
    initGps();
    initMQTT();  // Enable MQTT alongside WebSocket

    LOG_INFO("Setup complete - WebSocket + MQTT enabled");
    
    {
        const tflite::Model* model = tflite::GetModel(model_tflite);

        static tflite::MicroMutableOpResolver<5> resolver;
        resolver.AddFullyConnected();
        resolver.AddRelu();
        resolver.AddLogistic();

        static tflite::MicroInterpreter static_interpreter(
            model, resolver, tensor_arena, TENSOR_ARENA_SIZE, &micro_error_reporter
        );

        interpreter = &static_interpreter;

        if (interpreter->AllocateTensors() != kTfLiteOk) {
            LOG_ERROR("TFLite tensor allocation failed");
            return;
        }

        input = interpreter->input(0);
        output = interpreter->output(0);

        nn_initialized = true;
        LOG_INFO("TinyML model initialized successfully");
    }
}

void loop() {
    handleSerialData();
    
    WiFiManager::update();
    
    if (WiFiManager::isConnected()) {
        mqttLoop();  // MQTT connected clients
    }
    // Start HTTP and WebSocket servers once WiFi is ready
    static bool serversInitialized = false;
    if (!serversInitialized && WiFi.getMode() != WIFI_OFF) {
        // Wait a moment to ensure network stack is fully initialized
        static uint32_t lastCheckTime = 0;
        uint32_t now = millis();
        
        if (lastCheckTime == 0) {
            lastCheckTime = now;
        } else if (now - lastCheckTime > 2000) {  // Wait 2 seconds after first check
            WebServer::begin();
            LOG_INFO("[WebServer] HTTP server started on port %d", WIFI_AP_PORT);
            
            AsyncWebServer* httpServer = WebServer::getServer();
            if (httpServer) {
                WebSocketManager::init(httpServer, 81);
                LOG_INFO("[WebSocket] Server initialized on port 81");
            }
            
            serversInitialized = true;
        }
    }
    
    // Local server maintenance (WebSocket cleanup, vehicle timeouts)
    // localServerLoop(); // TODO: Will be enabled after migration
    
    // GPS and status updates (work in AP mode, no internet needed)
    updateGps();
    publishGatewayGpsStatusIfNeeded();
    publishNodeStatusPing();
    
    // GPS diagnostic monitor: log status every 30 seconds to help troubleshoot GPS issues
    static uint32_t lastGpsDiagnosticTime = 0;
    uint32_t now = millis();
    if (now - lastGpsDiagnosticTime > 30000) {
        lastGpsDiagnosticTime = now;
        String gpsStatus = getGpsStatus();
        if (gpsStatus == "NO_DATA") {
            LOG_WARN("[GPS DIAG] NO DATA - Module may not be connected. Check Serial2 pins (RX/TX on GPIO16/17)");
        } else if (gpsStatus == "SILENT") {
            LOG_WARN("[GPS DIAG] SILENT - Module not responding. Check baud rate (9600) or connection");
        } else if (gpsStatus == "SEARCHING") {
            static uint32_t searchStartTime = 0;
            if (searchStartTime == 0) searchStartTime = now;
            uint32_t searchDurationSec = (now - searchStartTime) / 1000;
            LOG_INFO("[GPS DIAG] SEARCHING for satellites (%u sec elapsed)...", searchDurationSec);
            if (searchDurationSec > 120) {
                LOG_ERROR("[GPS DIAG] No fix after 2+ minutes! Check GPS antenna, positioning, or hardware");
            }
        } else if (gpsStatus == "FIX") {
            LOG_INFO("[GPS DIAG] FIX OK - Lat: %.5f, Lon: %.5f", getGpsLatitude(), getGpsLongitude());
        }
    }
    
    // Broadcast gateway status via WebSocket every 10 seconds
    static uint32_t lastWebSocketStatusBroadcast = 0;
    if (now - lastWebSocketStatusBroadcast > 10000) {
        StaticJsonDocument<256> statusDoc;
        statusDoc["gateway_id"] = ConfigStorage::getDeviceID();
        statusDoc["uptime_ms"] = now;
        statusDoc["free_heap"] = ESP.getFreeHeap();
        statusDoc["rssi"] = WiFi.RSSI();
        statusDoc["wifi_clients"] = WiFi.softAPgetStationNum();
        
        String statusJson;
        serializeJson(statusDoc, statusJson);
        WebSocketManager::broadcastGatewayStatus(statusJson);
        
        lastWebSocketStatusBroadcast = now;
    }
    
    blinkLED();
}

void publishGatewayGpsStatusIfNeeded();
void publishNodeStatusPing();

float runTinyML(float temp, float humi, float accel)
{
    if (!nn_initialized || interpreter == nullptr) {
        return 0.0f;
    }

    static float t_raw = 0, h_raw = 0, a_raw = 0;
    static bool init = false;

    if (!init) {
        t_raw = temp;
        h_raw = humi;
        a_raw = accel;
        init = true;
    } else {
        // Smooth temp & humidity (slow-changing)
        t_raw = temp;
        h_raw = humi;
        // CRITICAL FIX: Don't smooth accel - preserve spikes for anomaly detection
        a_raw = accel;
    }

    // FIXED: Filter out negligible acceleration (noise suppression before model)
    if (fabs(a_raw) < 0.05f) {
        a_raw = 0.0f;
    }

    float t = scalerESP.norm(t_raw, scalerESP.t_mean, scalerESP.t_std);
    float h = scalerESP.norm(h_raw, scalerESP.h_mean, scalerESP.h_std);
    float a = scalerESP.norm(a_raw, scalerESP.a_mean, scalerESP.a_std);

    // ===== SCALER DISTRIBUTION CHECK =====
    // Warn if values deviate significantly from training distribution
    // Training mean ± 3*std should capture ~99.7% of normal data
    static uint32_t scaler_warn_count = 0;
    bool t_outlier = (fabs(t) > 4.0f);  // > 4 sigma
    bool h_outlier = (fabs(h) > 4.0f);
    bool a_outlier = (fabs(a) > 4.0f);
    
    if ((t_outlier || h_outlier || a_outlier) && (scaler_warn_count++ % 100 == 0)) {
        LOG_WARN("[Scaler] Distribution mismatch: t=%.2f h=%.2f a=%.2f (>4σ detected)", t, h, a);
        LOG_WARN("[Scaler] Check: model training vs runtime environment match");
    }

    Serial.printf("[NN INPUT] t=%.3f h=%.3f a=%.3f\n", t, h, a);

    input->data.f[0] = t;
    input->data.f[1] = h;
    input->data.f[2] = a;

    static uint32_t inference_count = 0;
    static uint32_t total_time_us = 0;
    static uint32_t min_time_us = UINT32_MAX;
    static uint32_t max_time_us = 0;

    // Start measurement
    unsigned long start_time = micros();

    // Execute inference
    if (interpreter->Invoke() != kTfLiteOk) {
        Serial.println("[NN] Invoke FAILED");
        return 0.0f;
    }

    // End measurement
    unsigned long end_time = micros();
    uint32_t elapsed_us = end_time - start_time;
    float elapsed_ms = elapsed_us / 1000.0f;

    // Update statistics
    inference_count++;
    total_time_us += elapsed_us;
    if (elapsed_us < min_time_us) min_time_us = elapsed_us;
    if (elapsed_us > max_time_us) max_time_us = elapsed_us;

    // Calculate average
    float avg_inference_ms = total_time_us / (1000.0f * inference_count);

    // Get score
    float score = output->data.f[0];

    Serial.printf("[NN RAW OUTPUT] %.6f (inference_time=%.3f ms)\n", score, elapsed_ms);

    // Clamp to [0, 1]
    if (score > 1.0f) score = 1.0f;
    if (score < 0.0f) score = 0.0f;

    return score;
}


void handleSerialData() {
    uint32_t now = millis();
    
    String line;
    if (!readRXLine(line)) {
        return;
    }

    RXPacket rxPacket;
    if (line.startsWith("[RX OK]")) {
        if (!parseRXLine(line, rxPacket)) {
            LOG_ERROR("Failed to parse valid RX line");
            return;
        }
    } else {
        String rawPayload = extractPayload(line);
        if (rawPayload.length() == 0) {
            rawPayload = line;
        }
        rawPayload.trim();

        if (rawPayload.length() == 0 ||
            !(rawPayload.startsWith("{") || isBase64Payload(rawPayload))) {
            return;
        }

        rxPacket.payload = rawPayload;
        rxPacket.node_id = 0;
    }

    // Decrypt payload if needed
    if (!decryptPayload(rxPacket)) {
        return;
    }

    // Verify HMAC signature
    String unsignedJson;
    if (!verifySignedJson(rxPacket.payload, unsignedJson)) {
        LOG_ERROR("HMAC verification failed for RX payload");
        int node_id = rxPacket.node_id;
        if (node_id > 0) {
            gVehicleStats.getStats(node_id).rejectedCount += 1;
        }
        return;
    }
    rxPacket.payload = unsignedJson;

    // Validate packet structure
    if (!validateRXPacket(rxPacket)) {
        LOG_ERROR("RX packet validation failed");
        int node_id = rxPacket.node_id;
        if (node_id > 0) {
            gVehicleStats.getStats(node_id).rejectedCount += 1;
        }
        return;
    }

    // Print RX packet debug info
    debugPrintRXPacket(rxPacket);

    // Check for heartbeat
    String dummy_id;
    if (parseHeartbeat(rxPacket.payload, dummy_id)) {
        LOG_INFO("Heartbeat received from: %s", rxPacket.payload.c_str());
        int node_id = rxPacket.node_id;
        if (node_id > 0) {
            gVehicleStats.getStats(node_id).packetCount += 1;
            gVehicleStats.getStats(node_id).totalCount += 1;
        }
        return;
    }

    // Parse telemetry
    TelemetryData telemetry;
    if (!parseJSONPayload(rxPacket.payload, telemetry)) {
        LOG_ERROR("JSON parse failed");
        int node_id = rxPacket.node_id;
        if (node_id > 0) {
            gVehicleStats.getStats(node_id).rejectedCount += 1;
        }
        return;
    }

    enrichTelemetryData(telemetry, rxPacket.rssi, rxPacket.snr,
                      rxPacket.sequence, rxPacket.received_at_ms);

    // Extract node_id for per-node detector tracking
    int node_id = rxPacket.node_id;

// ===== ADD SAMPLE TO WINDOW FIRST =====
if (node_id > 0) {
    nodeDetectors[node_id].addSample(
        telemetry.temperature,
        telemetry.humidity,
        telemetry.accel_magnitude
    );
}

// ===== ALWAYS RUN NN (even during warm-up) =====
float anomaly_score = runTinyML(
    telemetry.temperature,
    telemetry.humidity,
    telemetry.accel_magnitude
);

telemetry.nn_anomaly_score = anomaly_score;

// Update window score history (per-node)
if (node_id > 0) {
    nodeDetectors[node_id].updateWindowScore(anomaly_score);
}

// ===== CHECK IF READY (after updating score) =====
bool ready = (node_id > 0) ? nodeDetectors[node_id].isReady() : false;

if (!ready) {

    telemetry.anomaly_flag = 0;
    telemetry.nn_anomaly_state = "WARMUP";

    LOG_INFO("[Window] Warming up... score=%.3f", anomaly_score);

    // ===== WEBSOCKET BROADCAST EVEN DURING WARMUP =====
    StaticJsonDocument<512> wsTelemDoc;

    wsTelemDoc["type"] = "telemetry";
    wsTelemDoc["vehicle_id"] = telemetry.vehicle_id;
    wsTelemDoc["timestamp_ms"] = telemetry.timestamp_ms;

    wsTelemDoc["temperature"] = telemetry.temperature;
    wsTelemDoc["humidity"] = telemetry.humidity;
    wsTelemDoc["accel_magnitude"] = telemetry.accel_magnitude;

    wsTelemDoc["nn_anomaly_score"] = telemetry.nn_anomaly_score;
    wsTelemDoc["nn_anomaly_state"] = telemetry.nn_anomaly_state;

    wsTelemDoc["rssi"] = telemetry.rssi;
    wsTelemDoc["snr"] = telemetry.snr;

    String wsTelemJson;
    serializeJson(wsTelemDoc, wsTelemJson);

    WebSocketManager::broadcastTelemetry(wsTelemJson);

    if (node_id > 0) {
        gVehicleStats.getStats(node_id).packetCount += 1;
        gVehicleStats.getStats(node_id).totalCount += 1;
        gVehicleStats.getStats(node_id).lastTelemetry = now;
    }
    LOG_INFO("[Window] Warming up... score=%.3f (buffering sample)", anomaly_score);
    publishTelemetry(telemetry);

    return;
}

// ===== WINDOW-BASED DECISION (only when ready) =====
// Initialize per-node state if first time
if (nodeAnomalyState.find(node_id) == nodeAnomalyState.end()) {
    nodeAnomalyState[node_id] = false;
    nodeLastState[node_id] = false;
}

bool& is_anomaly = nodeAnomalyState[node_id];
bool& last_state = nodeLastState[node_id];

// Check if anomaly is confirmed (3/5 windows above threshold)
WindowFeatures feat =
    nodeDetectors[node_id].extractWindowFeatures();

bool rapid_temp_rise =
    feat.delta_temp > 8.0f;

bool strong_vibration =
    feat.max_accel_spike > 1.2f;

bool ml_consensus =
    (node_id > 0) ?
    nodeDetectors[node_id].isAnomalyConfirmed() :
    false;

bool feature_anomaly =
    rapid_temp_rise || strong_vibration;

bool anomaly_confirmed = ml_consensus || feature_anomaly;

bool normal_confirmed =
    (node_id > 0) ?
    nodeDetectors[node_id].isNormalConfirmed() :
    false;

// ===== SIMPLE HYSTERESIS WITH WINDOW CONSENSUS =====
if (anomaly_confirmed) {
    is_anomaly = true;
}
else if (normal_confirmed) {
    is_anomaly = false;
}
// Else: maintain previous state (transition zone)

// Logging detailed window-based decision
if (is_anomaly != last_state) {
    LOG_WARN("[WINDOW] State changed: %s → %s (score=%.3f, window_consensus)",
             last_state ? "ANOMALY" : "NORMAL",
             is_anomaly ? "ANOMALY" : "NORMAL",
             anomaly_score);
}
last_state = is_anomaly;

// ======================= OUTPUT =======================
telemetry.anomaly_flag = is_anomaly ? 1 : 0;
telemetry.nn_anomaly_state = is_anomaly ? "ANOMALY" : "NORMAL";

if (is_anomaly) {
    Serial.println("🚨🚨🚨 ANOMALY DETECTED (Window Consensus)");
}

LOG_INFO("[NN-Anomaly-Window] score=%.3f state=%s flag=%d (window_based_decision)",
         anomaly_score,
         telemetry.nn_anomaly_state.c_str(),
         telemetry.anomaly_flag);

    // ============ WebSocket Real-Time Telemetry Broadcast ============
    {
        StaticJsonDocument<512> wsTelemDoc;
        wsTelemDoc["type"] = "telemetry";
        wsTelemDoc["vehicle_id"] = telemetry.vehicle_id;
        wsTelemDoc["timestamp_ms"] = telemetry.timestamp_ms;
        wsTelemDoc["gps_latitude"] = telemetry.gps_latitude;
        wsTelemDoc["gps_longitude"] = telemetry.gps_longitude;
        wsTelemDoc["temperature"] = telemetry.temperature;
        wsTelemDoc["humidity"] = telemetry.humidity;
        wsTelemDoc["accel_magnitude"] = telemetry.accel_magnitude;
        wsTelemDoc["light_level"] = telemetry.light_level;
        wsTelemDoc["anomaly_flag"] = telemetry.anomaly_flag;
        wsTelemDoc["nn_anomaly_state"] = telemetry.nn_anomaly_state;
        wsTelemDoc["nn_anomaly_score"] = telemetry.nn_anomaly_score;
        wsTelemDoc["rssi"] = telemetry.rssi;
        wsTelemDoc["snr"] = telemetry.snr;
        wsTelemDoc["tamper_flag"] = telemetry.tamper_flag;
        
        String wsTelemJson;
        serializeJson(wsTelemDoc, wsTelemJson);
        WebSocketManager::broadcastTelemetry(wsTelemJson);
        
        LOG_DEBUG("[WebSocket-Telem] Broadcast to dashboard: vehicle=%s, anomaly=%s, score=%.3f",
                  telemetry.vehicle_id.c_str(), telemetry.nn_anomaly_state.c_str(), telemetry.nn_anomaly_score);
    }
    
    // ============ MQTT Telemetry Publish (parallel with WebSocket) ============
    if (WiFiManager::isConnected()) {
        publishTelemetry(telemetry);  // Send to MQTT broker simultaneously
        LOG_DEBUG("[MQTT-Telem] Published: vehicle=%s, score=%.3f",
                  telemetry.vehicle_id.c_str(), telemetry.nn_anomaly_score);
    }
    
    // ============ Handle Tamper Alerts (Tamper Flag) ============
    if (telemetry.tamper_flag != 0) {
        StaticJsonDocument<128> tamperAlertDoc;
        tamperAlertDoc["type"] = "tamper_alert";
        tamperAlertDoc["vehicle_id"] = telemetry.vehicle_id;
        tamperAlertDoc["timestamp_ms"] = telemetry.timestamp_ms;
        tamperAlertDoc["alert_reason"] = "Tamper Detected";
        
        String tamperJson;
        serializeJson(tamperAlertDoc, tamperJson);
        WebSocketManager::broadcastAlert(tamperJson);
        
        LOG_WARN("[TAMPER-ALERT] Vehicle %s: Tamper flag=%d detected", 
                 telemetry.vehicle_id.c_str(), telemetry.tamper_flag);
    }
    
    // ============ Handle Anomaly Alerts (NN Detection) ============
    if (is_anomaly && telemetry.anomaly_flag == 1) {
        StaticJsonDocument<256> anomalyAlertDoc;
        anomalyAlertDoc["type"] = "anomaly_alert";
        anomalyAlertDoc["vehicle_id"] = telemetry.vehicle_id;
        anomalyAlertDoc["timestamp_ms"] = telemetry.timestamp_ms;
        anomalyAlertDoc["alert_reason"] = "NN Anomaly Detected";
        anomalyAlertDoc["nn_anomaly_score"] = telemetry.nn_anomaly_score;
        anomalyAlertDoc["gps_latitude"] = telemetry.gps_latitude;
        anomalyAlertDoc["gps_longitude"] = telemetry.gps_longitude;
        anomalyAlertDoc["temperature"] = telemetry.temperature;
        
        String anomalyAlertJson;
        serializeJson(anomalyAlertDoc, anomalyAlertJson);
        WebSocketManager::broadcastAlert(anomalyAlertJson);
        
        LOG_WARN("[ANOMALY-BROADCAST] Vehicle %s: Score %.3f sent to Dashboard", 
                 telemetry.vehicle_id.c_str(), telemetry.nn_anomaly_score);
    }

    if (node_id > 0) {
        gVehicleStats.getStats(node_id).packetCount += 1;
        gVehicleStats.getStats(node_id).totalCount += 1;
        gVehicleStats.getStats(node_id).lastTelemetry = now;
    }
}

void blinkLED() {
    uint32_t now = millis();
    if (now - sys.lastBlinkTime > (sys.ledOn ? 100 : 900)) {
        sys.ledOn = !sys.ledOn;
        digitalWrite(LED_PIN, sys.ledOn ? HIGH : LOW);
        sys.lastBlinkTime = now;
    }
}


