#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <Arduino.h>
#include <AsyncWebSocket.h>
#include <ArduinoJson.h>

/**
 * @file websocket_server.h
 * @brief WebSocket Server cho hệ thống giám sát Local-Only
 * 
 * Module này quản lý WebSocket connections để broadcast dữ liệu thời gian thực
 * từ LoRa gateway tới tất cả các client (điện thoại, tablet, web app) đang kết nối
 * với mạng WiFi AP của ESP32.
 * 
 * Kiến trúc:
 * - ESP32 nhận gói tin LoRa → giải mã JSON
 * - Tạo WebSocket message → broadcast tới all connected clients
 * - Clients (Dashboard HTML) nhận → update UI real-time
 */

class WebSocketManager {
public:
    /**
     * @brief Khởi tạo WebSocket Server
     * @param server Con trỏ tới AsyncWebServer (từ web_server.cpp)
     * @param port Cổng WebSocket (mặc định: 81)
     */
    static void init(AsyncWebServer* server, uint16_t port = 81);
    
    /**
     * @brief Broadcast dữ liệu telemetry tới tất cả connected clients
     * @param telemetryJson Chuỗi JSON chứa dữ liệu từ bộ cảm biến
     * 
     * Định dạng telemetryJson:
     * {
     *   "device_id": "RX-GATEWAY-01",
     *   "node_id": 1,
     *   "timestamp": 1234567890000,
     *   "temperature": 28.5,
     *   "humidity": 65.2,
     *   "accel_mag": 0.8,
     *   "gps": {"lat": 21.123, "lon": 105.456, "alt": 15.2},
     *   "tamper_flag": 0,
     *   "anomaly_flag": 0,
     *   "anomaly_score": 0.42,
     *   "rssi": -78,
     *   "snr": 8.5,
     *   "sequence": 145
     * }
     */
    static void broadcastTelemetry(const String& telemetryJson);
    
    /**
     * @brief Broadcast trạng thái gateway
     * @param statusJson JSON chứa thông tin trạng thái
     * 
     * Định dạng statusJson:
     * {
     *   "gateway_id": "RX-GATEWAY-01",
     *   "uptime_ms": 3600000,
     *   "free_heap": 45000,
     *   "wifi_clients": 2,
     *   "sd_card_status": "ok",
     *   "sd_free_mb": 876,
     *   "cpu_freq_mhz": 80,
     *   "rssi": -65
     * }
     */
    static void broadcastGatewayStatus(const String& statusJson);
    
    /**
     * @brief Broadcast cảnh báo/sự kiện quan trọng
     * @param alertJson JSON chứa chi tiết cảnh báo
     * 
     * Định dạng alertJson:
     * {
     *   "type": "TAMPER|ANOMALY|SD_ERROR|WIFI_LOST",
     *   "severity": "INFO|WARN|CRITICAL",
     *   "node_id": 1,
     *   "message": "Hộp bị mở trái phép",
     *   "timestamp": 1234567890000
     * }
     */
    static void broadcastAlert(const String& alertJson);
    
    /**
     * @brief Lấy số client đang kết nối WebSocket
     * @return Số client hiện tại
     */
    static uint16_t getConnectedClientCount();
    
    /**
     * @brief Update thống kê WebSocket (tùy chọn)
     * @note Gọi từ loop() để quản lý keep-alive
     */
    static void update();
    
private:
    static AsyncWebSocket* ws;
    static uint16_t wsPort;
    static uint32_t messageCount;
};

#endif // WEBSOCKET_SERVER_H
