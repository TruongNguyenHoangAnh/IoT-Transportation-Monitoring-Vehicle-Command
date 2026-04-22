#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>

class WebServer {
public:
    static void init(uint16_t port = 80);
    static void begin();
    static void stop();
    static void update();

private:
    static AsyncWebServer* server;
    static uint16_t serverPort;
    
    // Route handlers
    static void handleStatus(AsyncWebServerRequest *request);
    static void handleWiFiScan(AsyncWebServerRequest *request);
    static void handleWiFiSet(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    static void handleRestart(AsyncWebServerRequest *request);
    static void handleResetWiFi(AsyncWebServerRequest *request);
    static void handleNotFound(AsyncWebServerRequest *request);
};

#endif // WEB_SERVER_H
