#include "websocket_server.h"
#include "web_server.h"
#include "config.h"
#include <AsyncWebSocket.h>

/**
 * @file websocket_server.cpp
 * @brief Triển khai WebSocket Server cho hệ thống Local-Only
 * 
 * Chức năng chính:
 * - Quản lý WebSocket connections từ các client
 * - Broadcast dữ liệu telemetry, status, alerts tới tất cả clients
 * - Handle connection/disconnection events
 * - Thống kê message count cho mục đích debug
 */

AsyncWebSocket* WebSocketManager::ws = nullptr;
uint16_t WebSocketManager::wsPort = 81;
uint32_t WebSocketManager::messageCount = 0;

/**
 * @brief Event handler cho WebSocket
 * 
 * Xử lý các sự kiện:
 * - WStype_CONNECT: Client kết nối
 * - WStype_DISCONNECT: Client ngắt kết nối
 * - WStype_TEXT: Client gửi text message
 * - WStype_BIN: Client gửi binary data
 */
static void onWebSocketEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type,
                              void * arg, uint8_t *data, size_t len) {
    
    switch (type) {
        case WS_EVT_CONNECT: {
            LOG_INFO("[WebSocket] Client kết nối: ID=%d", client->id());
            
            // Gửi message welcome cho client mới
            StaticJsonDocument<256> welcomeDoc;
            welcomeDoc["type"] = "WELCOME";
            welcomeDoc["message"] = "Đã kết nối tới Gateway RX Local";
            welcomeDoc["device_id"] = "RX-GATEWAY-01";
            welcomeDoc["timestamp"] = millis();
            
            String welcomeMsg;
            serializeJson(welcomeDoc, welcomeMsg);
            client->text(welcomeMsg);
            break;
        }
        
        case WS_EVT_DISCONNECT:
            LOG_INFO("[WebSocket] Client ngắt kết nối: ID=%d (tổng đã kết nối: %u)",
                     client->id(), server->count());
            break;
        
        case WS_EVT_ERROR:
            LOG_ERROR("[WebSocket] Lỗi từ client ID=%d", client->id());
            break;
        
        case WS_EVT_PONG:
            // Keep-alive pong response
            break;
        
        case WS_EVT_DATA: {
            // Optional: xử lý dữ liệu từ client
            // Hiện tại server chỉ broadcast theo một chiều (server → client)
            AwsFrameInfo * info = (AwsFrameInfo*)arg;
            String msg = "";
            
            if (info->final && info->index == 0 && info->len == len) {
                // Toàn bộ message trong một frame
                if (info->opcode == WS_TEXT) {
                    for(size_t i = 0; i < len; i++) {
                        msg += (char) data[i];
                    }
                    LOG_DEBUG("[WebSocket] Nhận từ client(%d): %s", client->id(), msg.c_str());
                }
            }
            break;
        }
    }
}

void WebSocketManager::init(AsyncWebServer* server, uint16_t port) {
    if (!server) {
        LOG_ERROR("[WebSocket] AsyncWebServer pointer null!");
        return;
    }
    
    wsPort = port;
    
    // Tạo WebSocket instance
    ws = new AsyncWebSocket("/ws");
    
    if (!ws) {
        LOG_ERROR("[WebSocket] Không thể tạo AsyncWebSocket!");
        return;
    }
    
    // Đặt event handler và attach vào server
    ws->onEvent(onWebSocketEvent);
    server->addHandler(ws);
    
    LOG_INFO("[WebSocket] Server khởi tạo thành công trên /ws");
}

void WebSocketManager::broadcastTelemetry(const String& telemetryJson) {
    if (!ws) {
        LOG_ERROR("[WebSocket] WebSocket không được khởi tạo!");
        return;
    }

    if (ws->count() > 0) {
        ws->textAll(telemetryJson);

        messageCount++;

        if (messageCount % 100 == 0) {
            LOG_DEBUG(
                "[WebSocket] Broadcast %u messages (%u clients)",
                messageCount,
                ws->count()
            );
        }
    }
}

void WebSocketManager::broadcastGatewayStatus(const String& statusJson) {
    if (!ws) {
        LOG_ERROR("[WebSocket] WebSocket không được khởi tạo!");
        return;
    }
    
    // Parse input JSON and add type field
    StaticJsonDocument<512> msgDoc;
    if (deserializeJson(msgDoc, statusJson) == DeserializationError::Ok) {
        msgDoc["type"] = "gateway_status";
    } else {
        LOG_ERROR("[WebSocket] Invalid gateway status JSON");
        return;
    }
    
    String output;
    serializeJson(msgDoc, output);
    
    if (ws->count() > 0) {
        ws->textAll(output);
        LOG_DEBUG("[WebSocket] Broadcast gateway_status: %s", output.c_str());
    }
}

void WebSocketManager::broadcastAlert(const String& alertJson) {
    if (!ws) {
        LOG_ERROR("[WebSocket] WebSocket không được khởi tạo!");
        return;
    }
    
    // Parse input JSON and preserve alert type
    StaticJsonDocument<512> alertDoc;
    if (deserializeJson(alertDoc, alertJson) != DeserializationError::Ok) {
        LOG_ERROR("[WebSocket] Failed to parse alert JSON");
        return;
    }
    
    // Ensure timestamp is present
    if (!alertDoc.containsKey("timestamp_ms")) {
        alertDoc["timestamp_ms"] = millis();
    }
    
    String output;
    serializeJson(alertDoc, output);
    
    if (ws->count() > 0) {
        ws->textAll(output);
        LOG_WARN("[WebSocket] 🚨 Broadcast cảnh báo tới %u clients", ws->count());
    }
}

uint16_t WebSocketManager::getConnectedClientCount() {
    if (!ws) return 0;
    return ws->count();
}

void WebSocketManager::update() {
    // WebSocket được quản lý bởi AsyncWebServer
    // Hàm này có thể dùng cho keep-alive hoặc periodic tasks
    if (ws && messageCount % 10000 == 0) {
        LOG_DEBUG("[WebSocket] Thống kê: %u messages, %u clients kết nối",
                  messageCount, ws->count());
    }
}
