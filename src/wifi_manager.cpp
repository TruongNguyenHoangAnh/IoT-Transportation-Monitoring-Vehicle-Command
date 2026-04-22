#include "wifi_manager.h"
#include "config_storage.h"
#include "web_server.h"
#include "config.h"
#include <ArduinoJson.h>

ESPState WiFiManager::currentState = STATE_BOOT;
uint32_t WiFiManager::apStartTime = 0;
uint32_t WiFiManager::staStartTime = 0;
uint32_t WiFiManager::buttonPressTime = 0;
bool WiFiManager::buttonPressed = false;

void WiFiManager::init() {
    // CRITICAL: Wait for TCP/IP stack initialization
    // This prevents "assert failed: tcpip_api_call Invalid mbox" crash
    delay(1000);
    
    // IMPORTANT: Disable WiFi sleep to prevent MQTT drops, ping timeout, packet loss
    WiFi.setSleep(false);
    
    ConfigStorage::init();
    pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
    
    currentState = STATE_BOOT;
    apStartTime = millis();
    
    // Debug: Log startup state
    bool hasConfig = ConfigStorage::hasWiFiConfig();
    LOG_INFO("[WiFiMgr] Boot: saved_config=%s, device_id=%s", 
        hasConfig ? "YES" : "NO",
        ConfigStorage::getDeviceID().c_str());
}

void WiFiManager::update() {
    checkButtonPress();
    handleStateTransition();
}

void WiFiManager::handleStateTransition() {
    uint32_t now = millis();
    
    switch (currentState) {
        case STATE_BOOT:
            // CRITICAL: Wait 3+ seconds for TCP/IP stack initialization before WiFi API calls
            // This prevents "assert failed: tcpip_api_call Invalid mbox" crash
            if (now - apStartTime > 3000) {
                if (ConfigStorage::hasWiFiConfig()) {
                    LOG_INFO("[WiFiMgr] STATE_BOOT → STATE_CONNECTING_STA (saved config found)");
                    transitionToSTA();
                } else {
                    LOG_INFO("[WiFiMgr] STATE_BOOT → STATE_CONFIG_AP (no saved config, starting AP)");
                    transitionToAP();
                }
            }
            break;
            
        case STATE_CONFIG_AP:
            // Check timeout: 120 seconds
            if (now - apStartTime > WIFI_AP_TIMEOUT_MS) {
                ESP.restart();
            }
            break;
            
        case STATE_CONNECTING_STA:
            // Check timeout: 20 seconds
            if (now - staStartTime > WIFI_CONNECT_TIMEOUT_MS) {
                if (WiFi.status() != WL_CONNECTED) {
                    wl_status_t status = WiFi.status();
                    LOG_WARN("[WiFiMgr] WiFi connect timeout. Status: %d (0=Idle, 1=NoSSID, 3=Connected, 4=Failed, 6=Disconnected)", (int)status);
                    
                    uint8_t attempts = ConfigStorage::getWiFiAttempts();
                    if (attempts >= WIFI_RECONNECT_ATTEMPTS) {
                        ConfigStorage::resetWiFiAttempts();
                        // CRITICAL: Reset WiFi before falling back to AP
                        WiFi.disconnect(true);
                        delay(300);
                        transitionToAP();
                    } else {
                        ConfigStorage::incrementWiFiAttempts();
                        transitionToSTA();
                    }
                }
            }
            
            // Check if connected
            if (WiFi.status() == WL_CONNECTED) {
                ConfigStorage::resetWiFiAttempts();
                transitionToRunning();
            }
            break;
            
        case STATE_RUNNING: {
            // Monitor connection with debounce to prevent reconnect spam
            static uint32_t disconnectTime = 0;
            
            if (WiFi.status() != WL_CONNECTED) {
                if (disconnectTime == 0) {
                    disconnectTime = millis();
                    wl_status_t status = WiFi.status();
                    LOG_WARN("[WiFiMgr] WiFi disconnected. Status: %d", (int)status);
                }
                
                // Only reconnect after 5 seconds of disconnection (debounce)
                if (millis() - disconnectTime > 5000) {
                    LOG_WARN("[WiFiMgr] Reconnecting to STA after 5s debounce");
                    // Reset retry counter when starting new reconnection attempt
                    ConfigStorage::resetWiFiAttempts();
                    transitionToSTA();
                    disconnectTime = 0;
                }
            } else {
                disconnectTime = 0;
            }
            break;
        }
            
        default:
            break;
    }
}

void WiFiManager::transitionToAP() {
    if (currentState == STATE_CONFIG_AP) return;
    
    currentState = STATE_CONFIG_AP;
    apStartTime = millis();
    
    LOG_INFO("[WiFiMgr] Entering AP mode: %s", WIFI_AP_SSID);
    
    // CRITICAL: Complete WiFi cleanup before switching modes
    WiFi.disconnect(true, true);  // Disconnect both AP and STA, turn off radio
    delay(200);                     // Wait for radio to power down
    
    WiFi.mode(WIFI_OFF);           // Ensure completely off first
    delay(100);
    
    // FIX: Use WIFI_AP_STA to allow safe WiFi scanning while in AP mode
    WiFi.mode(WIFI_AP_STA);
    delay(200);
    
    // Set TX power for good AP signal (max 20.5 dBm)
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    
    IPAddress apIP(WIFI_AP_IP);
    IPAddress apGateway(WIFI_AP_GATEWAY);
    IPAddress apSubnet(WIFI_AP_SUBNET);
    
    WiFi.softAPConfig(apIP, apGateway, apSubnet);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    
    delay(200);
    
    // CRITICAL: Start WebServer AFTER WiFi is ready in AP mode
    WebServer::begin();
    
    LOG_INFO("[WiFiMgr] AP started at %s (192.168.4.1:80)", WIFI_AP_SSID);
}

void WiFiManager::transitionToSTA() {
    if (currentState == STATE_CONNECTING_STA) return;
    
    currentState = STATE_CONNECTING_STA;
    staStartTime = millis();
    
    // CRITICAL: Complete WiFi cleanup before switching modes
    WiFi.disconnect(true, true);  // Disconnect from AP and STA, turn off radio
    delay(500);                     // IMPORTANT: Need longer delay for stack
    
    WiFi.mode(WIFI_OFF);           // Ensure completely off first
    delay(300);                     // IMPORTANT: Longer delay needed
    
    WiFi.mode(WIFI_STA);
    delay(500);  // CRITICAL: WiFi stack needs substantial time after mode switch
    
    // IMPORTANT: WiFi settings MUST be called AFTER mode switch, BEFORE WiFi.begin()
    WiFi.setSleep(false);           // Disable sleep to prevent MQTT drops
    WiFi.setAutoReconnect(true);    // Auto-reconnect on disconnection
    WiFi.persistent(false);         // DON'T use NVS storage (use ConfigStorage instead)
    WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Good signal strength
    
    String ssid = ConfigStorage::getWiFiSSID();
    String password = ConfigStorage::getWiFiPassword();
    
    // IMPORTANT: Validate credentials before attempting connection
    if (ssid.isEmpty()) {
        LOG_ERROR("[WiFiMgr] SSID is empty - cannot connect to STA");
        transitionToAP();
        return;
    }
    
    if (password.length() < 8) {
        LOG_ERROR("[WiFiMgr] Password is invalid (must be 8+ chars)");
        transitionToAP();
        return;
    }
    
    LOG_INFO("[WiFiMgr] Connecting to SSID: %s", ssid.c_str());
    LOG_INFO("[WiFiMgr] Password len: %d", password.length());
    
    WiFi.begin(ssid.c_str(), password.c_str());
}

void WiFiManager::transitionToRunning() {
    currentState = STATE_RUNNING;
    LOG_INFO("[WiFiMgr] Connected! IP: %s | SSID: %s | RSSI: %d dBm", 
        WiFi.localIP().toString().c_str(),
        WiFi.SSID().c_str(),
        WiFi.RSSI());
}

void WiFiManager::startAPMode() {
    transitionToAP();
}

void WiFiManager::startSTAMode() {
    transitionToSTA();
}

void WiFiManager::stopAPMode() {
    if (currentState == STATE_CONFIG_AP) {
        WiFi.mode(WIFI_OFF);
        currentState = STATE_BOOT;
    }
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::isInAPMode() {
    return currentState == STATE_CONFIG_AP;
}

ESPState WiFiManager::getCurrentState() {
    return currentState;
}

std::vector<WiFiNetwork> WiFiManager::scanNetworks() {
    std::vector<WiFiNetwork> networks;
    
    // Guard: ensure WiFi mode is properly set before scanning
    if (WiFi.getMode() == WIFI_OFF) {
    Serial.printf("[WiFiMgr] scanNetworks: WiFi mode is OFF, returning empty\n");
        return networks;  // Return empty list if WiFi not initialized
    }
    
    Serial.printf("[WiFiMgr] scanNetworks: Starting scan (mode=%d)\n", WiFi.getMode());
    
    // Clean previous scan results
    WiFi.scanDelete();
    delay(100);
    
    uint32_t scanStart = millis();
    int n = WiFi.scanNetworks(false, false, false, WIFI_SCAN_TIMEOUT_MS / 1000);
    uint32_t scanTime = millis() - scanStart;
    
    Serial.printf("[WiFiMgr] scanNetworks: Found %d networks in %lu ms (timeout: %d sec)\n", n, scanTime, WIFI_SCAN_TIMEOUT_MS / 1000);
    
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            networks.push_back({
                WiFi.SSID(i),
                WiFi.RSSI(i),
                WiFi.encryptionType(i) != WIFI_AUTH_OPEN,
                (uint8_t)WiFi.channel(i)
            });
        }
        
        // Remove duplicates - keep only strongest signal per SSID
        std::vector<WiFiNetwork> unique_networks;
        for (const auto& net : networks) {
            auto it = std::find_if(unique_networks.begin(), unique_networks.end(),
                [&net](const WiFiNetwork& u) { return u.ssid == net.ssid; });
            
            if (it == unique_networks.end()) {
                // SSID not found - add it
                unique_networks.push_back(net);
            } else if (net.rssi > it->rssi) {
                // Found but current signal is stronger - replace
                *it = net;
            }
        }
        
        // Sort by RSSI (strongest first)
        std::sort(unique_networks.begin(), unique_networks.end(),
            [](const WiFiNetwork& a, const WiFiNetwork& b) {
                return a.rssi > b.rssi;  // Descending order
            });
        
        // Keep only top 20
        if (unique_networks.size() > 20) {
            unique_networks.resize(20);
        }
        
        networks = unique_networks;
    }
    
    // Clean up results after reading
    WiFi.scanDelete();
    return networks;
}

bool WiFiManager::setWiFiCredentials(const String& ssid, const String& password) {
    if (ssid.isEmpty() || password.length() < 8) {
        return false;
    }
    
    ConfigStorage::setWiFiConfig(ssid, password);
    ConfigStorage::resetWiFiAttempts();
    return true;
}

bool WiFiManager::restartESP() {
    ESP.restart();
    return true;
}

String WiFiManager::getStatusJSON() {
    StaticJsonDocument<256> doc;
    
    doc["device"] = ConfigStorage::getDeviceID();
    doc["mode"] = currentState == STATE_CONFIG_AP ? "AP" : "STA";
    doc["state"] = (int)currentState;
    doc["wifi_saved"] = ConfigStorage::hasWiFiConfig();
    doc["connected"] = isConnected();
    doc["ip"] = getDeviceIP();
    doc["ssid"] = WiFi.SSID();
    doc["ap_ssid"] = WIFI_AP_SSID;
    doc["rssi"] = WiFi.RSSI();
    doc["free_heap"] = ESP.getFreeHeap();
    
    String output;
    serializeJson(doc, output);
    return output;
}

String WiFiManager::getDeviceIP() {
    if (currentState == STATE_CONFIG_AP) {
        return WiFi.softAPIP().toString();
    } else if (isConnected()) {
        return WiFi.localIP().toString();
    }
    return "0.0.0.0";
}

void WiFiManager::checkButtonPress() {
    int buttonState = digitalRead(CONFIG_BUTTON_PIN);
    uint32_t now = millis();
    
    if (buttonState == LOW) {
        if (!buttonPressed) {
            buttonPressed = true;
            buttonPressTime = now;
            LOG_INFO("[WiFiMgr] Button pressed at %lu ms", now);
        }
        
        // Trigger AP mode on 5-second hold
        if (now - buttonPressTime >= CONFIG_BUTTON_HOLD_MS) {
            LOG_WARN("[WiFiMgr] Button hold 5s detected - clearing config and reset to AP");
            ConfigStorage::clearWiFiConfig();
            transitionToAP();
            buttonPressed = false;
        }
    } else {
        buttonPressed = false;
    }
}
