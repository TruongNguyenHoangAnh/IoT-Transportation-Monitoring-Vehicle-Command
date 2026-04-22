#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include <Preferences.h>
#include <String.h>

class ConfigStorage {
public:
    static void init();
    
    static bool hasWiFiConfig();
    static String getWiFiSSID();
    static String getWiFiPassword();
    static void setWiFiConfig(const String& ssid, const String& password);
    static void clearWiFiConfig();
    
    static String getDeviceID();
    static void setDeviceID(const String& id);
    
    static String getMqttServer();
    static String getMqttPort();
    static void setMqttConfig(const String& server, const String& port);
    
    static uint8_t getWiFiAttempts();
    static void incrementWiFiAttempts();
    static void resetWiFiAttempts();
    
    static bool hasMQTTConfig();
    static void debugDump();

private:
    static const char* NAMESPACE;
};

#endif // CONFIG_STORAGE_H
