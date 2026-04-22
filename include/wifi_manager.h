#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <String.h>
#include <vector>

enum ESPState {
    STATE_BOOT,
    STATE_CONFIG_AP,
    STATE_CONNECTING_STA,
    STATE_RUNNING,
    STATE_ERROR
};

struct WiFiNetwork {
    String ssid;
    int32_t rssi;
    bool secure;
    uint8_t channel;
};

class WiFiManager {
public:
    static void init();
    static void update();
    static void handleStateTransition();
    
    // State queries
    static ESPState getCurrentState();
    static bool isConnected();
    static bool isInAPMode();
    
    // Configuration
    static void startAPMode();
    static void startSTAMode();
    static void stopAPMode();
    
    // WiFi operations
    static std::vector<WiFiNetwork> scanNetworks();
    static bool setWiFiCredentials(const String& ssid, const String& password);
    static bool restartESP();
    
    // Status
    static String getStatusJSON();
    static String getDeviceIP();
    
    // Button handling
    static void checkButtonPress();

private:
    static ESPState currentState;
    static uint32_t apStartTime;
    static uint32_t staStartTime;
    static uint32_t buttonPressTime;
    static bool buttonPressed;
    
    static void onSTAConnected();
    static void onSTADisconnected();
    static void transitionToAP();
    static void transitionToSTA();
    static void transitionToRunning();
};

#endif // WIFI_MANAGER_H
