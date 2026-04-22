#include "config_storage.h"
#include "config.h"

const char* ConfigStorage::NAMESPACE = "config";

static Preferences gPrefs;

void ConfigStorage::init() {
    gPrefs.begin(NAMESPACE, false);
    
    if (!gPrefs.isKey("device_id")) {
        gPrefs.putString("device_id", "RX-Gateway");
    }
    
    if (!gPrefs.isKey("mqtt_server")) {
        gPrefs.putString("mqtt_server", MQTT_BROKER);
    }
    
    if (!gPrefs.isKey("mqtt_port")) {
        gPrefs.putString("mqtt_port", String(MQTT_PORT));
    }
    
    LOG_INFO("[Config] Init complete - Preferences persistent");
}

bool ConfigStorage::hasWiFiConfig() {
    String ssid = gPrefs.getString("wifi_ssid", "");
    return !ssid.isEmpty();
}

String ConfigStorage::getWiFiSSID() {
    return gPrefs.getString("wifi_ssid", "");
}

String ConfigStorage::getWiFiPassword() {
    return gPrefs.getString("wifi_pass", "");
}

void ConfigStorage::setWiFiConfig(const String& ssid, const String& password) {
    gPrefs.putString("wifi_ssid", ssid);
    gPrefs.putString("wifi_pass", password);
    gPrefs.putUChar("wifi_attempts", 0);
    LOG_INFO("[Config] WiFi saved - SSID: %s (attempts reset)", ssid.c_str());
}

void ConfigStorage::clearWiFiConfig() {
    gPrefs.remove("wifi_ssid");
    gPrefs.remove("wifi_pass");
    gPrefs.putUChar("wifi_attempts", 0);
}

String ConfigStorage::getDeviceID() {
    return gPrefs.getString("device_id", "RX-Gateway");
}

void ConfigStorage::setDeviceID(const String& id) {
    gPrefs.putString("device_id", id);
}

String ConfigStorage::getMqttServer() {
    return gPrefs.getString("mqtt_server", "10.16.1.75");
}

String ConfigStorage::getMqttPort() {
    return gPrefs.getString("mqtt_port", "1883");
}

void ConfigStorage::setMqttConfig(const String& server, const String& port) {
    gPrefs.putString("mqtt_server", server);
    gPrefs.putString("mqtt_port", port);
    LOG_INFO("[Config] MQTT saved - Server: %s:%s", server.c_str(), port.c_str());
}

uint8_t ConfigStorage::getWiFiAttempts() {
    return gPrefs.getUChar("wifi_attempts", 0);
}

void ConfigStorage::incrementWiFiAttempts() {
    uint8_t attempts = gPrefs.getUChar("wifi_attempts", 0);
    // CRITICAL: Cap at 10 to prevent infinite retry loop
    if (attempts < 10) {
        gPrefs.putUChar("wifi_attempts", attempts + 1);
    }
}

void ConfigStorage::resetWiFiAttempts() {
    gPrefs.putUChar("wifi_attempts", 0);
}

bool ConfigStorage::hasMQTTConfig() {
    String server = gPrefs.getString("mqtt_server", "");
    return !server.isEmpty();
}

void ConfigStorage::debugDump() {
    LOG_INFO("===== CONFIG DUMP =====");
    LOG_INFO("SSID: %s | MQTT: %s:%s | DeviceID: %s | Attempts: %d", 
        gPrefs.getString("wifi_ssid", "<none>").c_str(),
        gPrefs.getString("mqtt_server", "<none>").c_str(),
        gPrefs.getString("mqtt_port", "1883").c_str(),
        gPrefs.getString("device_id", "RX-Gateway").c_str(),
        gPrefs.getUChar("wifi_attempts", 0));
    LOG_INFO("====== END DUMP ======");
}
