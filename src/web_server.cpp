#include "web_server.h"
#include "wifi_manager.h"
#include "config_storage.h"
#include "web_ui.h"
#include <ArduinoJson.h>
#include <algorithm>
#include "esp_timer.h"

AsyncWebServer* WebServer::server = nullptr;
uint16_t WebServer::serverPort = 80;

void WebServer::init(uint16_t port) {
    serverPort = port;
    server = new AsyncWebServer(port);
    
    // GET / - Serve index.html
    server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", index_html);
    });
    
    // GET /index - Same as /
    server->on("/index", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", index_html);
    });
    
    // GET /status
    server->on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        handleStatus(request);
    });
    
    // GET /scan
    server->on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        handleWiFiScan(request);
    });
    
    // POST /wifi
    server->on("/wifi", HTTP_POST, 
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleWiFiSet(request, data, len, index, total);
        }
    );
    
    // POST /restart
    server->on("/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        handleRestart(request);
    });
    
    // POST /reset - Reset WiFi config and restart
    server->on("/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        handleResetWiFi(request);
    });
    
    // Catch-all for 404
    server->onNotFound([](AsyncWebServerRequest *request) {
        handleNotFound(request);
    });
}

void WebServer::begin() {
    if (server) {
        server->begin();
    }
}

void WebServer::stop() {
    if (server) {
        server->end();
    }
}

void WebServer::update() {
    // AsyncWebServer handles updates automatically
}

void WebServer::handleStatus(AsyncWebServerRequest *request) {
    String json = WiFiManager::getStatusJSON();
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
    response->addHeader("Content-Type", "application/json");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

void WebServer::handleWiFiScan(AsyncWebServerRequest *request) {

    // Call scanNetworks directly - it handles WiFi.scanDelete and delays internally
    std::vector<WiFiNetwork> networks = WiFiManager::scanNetworks();
    Serial.printf("[WebServer] Scan returned %d networks\n", (int)networks.size());
    
    StaticJsonDocument<2048> doc;
    JsonArray array = doc.to<JsonArray>();
    
    // Sort by RSSI (strongest first)
    std::sort(networks.begin(), networks.end(), 
        [](const WiFiNetwork& a, const WiFiNetwork& b) {
            return a.rssi > b.rssi;
        }
    );
    
    for (const auto& net : networks) {
        JsonObject obj = array.createNestedObject();
        obj["ssid"] = net.ssid;
        obj["rssi"] = net.rssi;
        obj["secure"] = net.secure;
        obj["channel"] = net.channel;
    }
    
    String output;
    serializeJson(doc, output);
    Serial.printf("[WebServer] Response JSON size: %d bytes\n", (int)output.length());
    
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", output);
    response->addHeader("Content-Type", "application/json");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    
    Serial.printf("[WebServer] /scan response sent\n");
}

void WebServer::handleWiFiSet(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Only process on last chunk
    if (index + len != total) {
        return;
    }
    
    StaticJsonDocument<256> responseDoc;
    
    // Parse JSON payload
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
        responseDoc["status"] = "error";
        responseDoc["message"] = "Invalid JSON";
        String response;
        serializeJson(responseDoc, response);
        request->send(400, "application/json", response);
        return;
    }
    
    // Validate fields
    if (!doc.containsKey("ssid") || !doc.containsKey("password")) {
        responseDoc["status"] = "error";
        responseDoc["message"] = "Missing ssid or password";
        String response;
        serializeJson(responseDoc, response);
        request->send(400, "application/json", response);
        return;
    }
    
    String ssid = doc["ssid"];
    String password = doc["password"];
    
    // Validate credentials
    if (ssid.length() == 0 || ssid.length() > 32) {
        responseDoc["status"] = "error";
        responseDoc["message"] = "Invalid SSID length (1-32)";
        String response;
        serializeJson(responseDoc, response);
        request->send(400, "application/json", response);
        return;
    }
    
    if (password.length() < 8 || password.length() > 63) {
        responseDoc["status"] = "error";
        responseDoc["message"] = "Password must be 8-63 characters";
        String response;
        serializeJson(responseDoc, response);
        request->send(400, "application/json", response);
        return;
    }
    
    // Save and prepare response
    if (WiFiManager::setWiFiCredentials(ssid, password)) {
        responseDoc["status"] = "saved";
        responseDoc["restart"] = true;
        responseDoc["message"] = "WiFi configured, restarting...";
        
        String response;
        serializeJson(responseDoc, response);
        request->send(200, "application/json", response);
        
        // CRITICAL FIX: Use async timer instead of blocking delay()
        // delay() in AsyncWebServer callback blocks TCP task → lwIP crash
        esp_timer_handle_t timer;
        esp_timer_create_args_t args = {
            .callback = [](void*) { ESP.restart(); },
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "restart_timer"
        };
        
        if (esp_timer_create(&args, &timer) == ESP_OK) {
            esp_timer_start_once(timer, 500000);  // 500ms delay, async
        }
    } else {
        responseDoc["status"] = "error";
        responseDoc["message"] = "Failed to save credentials";
        String response;
        serializeJson(responseDoc, response);
        request->send(500, "application/json", response);
    }
}

void WebServer::handleRestart(AsyncWebServerRequest *request) {
    StaticJsonDocument<128> doc;
    doc["status"] = "restarting";
    doc["message"] = "ESP32 restarting in 1 second...";
    
    String response;
    serializeJson(doc, response);
    
    AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", response);
    resp->addHeader("Connection", "close");
    request->send(resp);
    
    // CRITICAL FIX: Use async timer instead of blocking delay()
    // delay() in AsyncWebServer callback blocks TCP task → lwIP crash
    esp_timer_handle_t timer;
    esp_timer_create_args_t args = {
        .callback = [](void*) { ESP.restart(); },
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "restart_timer"
    };
    
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        esp_timer_start_once(timer, 1000000);  // 1s delay, async
    }
}

void WebServer::handleResetWiFi(AsyncWebServerRequest *request) {
    StaticJsonDocument<128> doc;
    doc["status"] = "reset_initiated";
    doc["message"] = "WiFi config cleared - Restarting to AP mode...";
    
    String response;
    serializeJson(doc, response);
    
    AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", response);
    resp->addHeader("Connection", "close");
    request->send(resp);
    
    // Clear WiFi config and restart via async timer
    esp_timer_handle_t timer;
    esp_timer_create_args_t args = {
        .callback = [](void*) { 
            ConfigStorage::clearWiFiConfig();
            ESP.restart(); 
        },
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "reset_wifi_timer"
    };
    
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        esp_timer_start_once(timer, 1000000);  // 1s delay, async
    }
}

void WebServer::handleNotFound(AsyncWebServerRequest *request) {
    StaticJsonDocument<256> doc;
    doc["error"] = "Not Found";
    doc["path"] = request->url();
    doc["method"] = request->methodToString();
    
    String response;
    serializeJson(doc, response);
    request->send(404, "application/json", response);
}
