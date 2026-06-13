#include "web_server.h"
#include "wifi_manager.h"
#include "config_storage.h"
#include "web_ui.h"
#include <ArduinoJson.h>
#include <algorithm>
#include "esp_timer.h"

// ============ DASHBOARD HTML (MINIMAL) ============
const char dashboard_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>RX Gateway Dashboard</title>
<style>body{margin:0;padding:20px;background:#f5f5f5;color:#333;font-family:monospace}
.header{background:#2d5016;border-bottom:3px solid #22a833;padding:15px;margin-bottom:20px;border-radius:8px}
h1{color:#fff;margin:0}
.status{color:#1db10d;font-weight:bold;margin-top:10px;font-size:14px}
.status.error{color:#ff3333}
.row{display:flex;gap:20px;margin:10px 0}
.col{flex:1}
.card{background:#fff;border:2px solid #22a833;padding:15px;border-radius:6px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}
.metric{display:flex;justify-content:space-between;padding:8px;margin:5px 0;background:#f0f8f0;border-radius:4px}
.metric-label{color:#666;font-weight:bold}
.metric-value{color:#1db10d;font-weight:bold;font-family:Courier;font-size:15px}
h2{color:#00d4ff;margin:0 0 10px 0;font-size:16px}
.vehicle-item{padding:10px;margin:5px 0;background:#f0f8f0;border-left:4px solid #22a833;border-radius:4px;display:flex;justify-content:space-between;align-items:center}
.vehicle-name{font-weight:bold;color:#333}
.vehicle-status{font-weight:bold;font-size:12px}
.status-online{color:#1db10d}
.status-offline{color:#ff3333}
table{width:100%;border-collapse:collapse;margin-top:10px;font-size:12px}
th,td{padding:8px;text-align:left;border-bottom:1px solid #ddd}
th{background:#22a833;color:#fff;font-weight:bold}
td{color:#333}
.anomaly{color:#ff3333;font-weight:bold}
.normal{color:#1db10d;font-weight:bold}
#alertsList{max-height:150px;overflow-y:auto}
.alert{padding:8px;margin:5px 0;background:rgba(255,51,51,0.2);border-left:4px solid #ff3333;border-radius:3px;font-size:11px;color:#c00}
#vehicleList{min-height:50px}
</style></head><body>
<div class="header"><h1>RX GATEWAY LOCAL DASHBOARD</h1>
<div class="status" id="wsStatus">🔴 Kết nối WebSocket...</div></div>

<div class="row">
  <div class="col">
    <div class="card">
      <h2 style="margin-top:0">Thông Tin Gateway</h2>
      <div class="metric"><span class="metric-label">Gateway ID:</span><span class="metric-value" id="gwId">-</span></div>
      <div class="metric"><span class="metric-label">Heap Free:</span><span class="metric-value" id="heap">-</span></div>
      <div class="metric"><span class="metric-label">WiFi RSSI:</span><span class="metric-value" id="rssi">-</span></div>
      <div class="metric"><span class="metric-label">Uptime:</span><span class="metric-value" id="uptime">-</span></div>
    </div>
  </div>
  <div class="col">
    <div class="card">
      <h2 style="margin-top:0">Trạng Thái Xe Kết Nối</h2>
      <div id="vehicleList" style="min-height:80px">Chờ dữ liệu...</div>
    </div>
  </div>
</div>

<div class="card">
  <h2>Cảnh Báo - Dữ Liệu Cảm Biến Bất Thường</h2>
  <div id="alertsList" style="min-height:50px">Không có cảnh báo</div>
</div>

<div class="card">
  <h2>Real-Time Telemetry (10 Dữ Liệu Gần Nhất)</h2>
  <table>
    <thead><tr><th>Thời Gian</th><th>Xe</th><th>Nhiệt Độ</th><th>Độ Ẩm</th><th>Accel</th><th>Tamper</th><th>GPS</th><th>SNR</th><th>RSSI</th></tr></thead>
    <tbody id="telemetryBody"><tr><td colspan="9">Chờ dữ liệu...</td></tr></tbody>
  </table>
</div>

<script>
const wsUrl=`ws://${window.location.host}/ws`;
console.log('[WS] Kết nối tới:', wsUrl);
const ws=new WebSocket(wsUrl);
let telemData=[];
let vehicleStatus={};
let vehicleLastSeen={};
const VEHICLE_TIMEOUT_MS=4500;
setInterval(()=>{
    const now=Date.now();
    let updated=false;
    for(const [id,online] of Object.entries(vehicleStatus)){
        const lastSeen=vehicleLastSeen[id]||0;
        if(online && now-lastSeen>VEHICLE_TIMEOUT_MS){
            vehicleStatus[id]=false;
            updated=true;
        }
    }
    if(updated)updateVehicleList()
},5000);
function updateWebSocketStatus(connected){
    const st=document.getElementById('wsStatus');
    if(connected){st.textContent='🟢 WebSocket Kết Nối';st.className='status'}
    else{st.textContent='🔴 WebSocket Ngắt';st.className='status error'}
}
function updateVehicleList(){
    const list=document.getElementById('vehicleList');
    const vehicles=Object.entries(vehicleStatus).sort();
    if(vehicles.length===0){list.innerHTML='Không có xe kết nối';return}
    list.innerHTML=vehicles.map(([id,online])=>`
        <div class="vehicle-item">
            <span class="vehicle-name">${id}</span>
            <span class="vehicle-status ${online?'status-online':'status-offline'}">${online?'🟢 ONLINE':'🔴 OFFLINE'}</span>
        </div>
    `).join('')
}
function formatTime(ms){const d=new Date(ms);return d.toLocaleString('vi-VN',{timeZone:'Asia/Ho_Chi_Minh',year:'numeric',month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',second:'2-digit'})}
function addTelemetry(t){
    console.log('[Telemetry] Nhận:', t);
    t.received_at = Date.now();
    const vehicleId=t.vehicle_id||'?';
    vehicleStatus[vehicleId]=true;
    vehicleLastSeen[vehicleId]=Date.now();
    updateVehicleList();
    
    // Check for anomaly and add alert
    if (t.anomaly_flag === 1 || t.nn_anomaly_score > 0.5) {
        const alertMsg = {
            type: 'anomaly_alert',
            vehicle_id: vehicleId,
            alert_reason: 'NN Anomaly Detected',
            nn_anomaly_score: t.nn_anomaly_score
        };
        addAlert(alertMsg);
    }
    
    // Check for tamper and add alert
    if (t.tamper_flag === 1) {
        const alertMsg = {
            type: 'tamper_alert',
            vehicle_id: vehicleId,
            alert_reason: 'Tamper/Intrusion Detected'
        };
        addAlert(alertMsg);
    }
    
    telemData.unshift(t);
    if(telemData.length>10)telemData.pop();
    const tbody=document.getElementById('telemetryBody');
    tbody.innerHTML=telemData.map((row,i)=>{const gps=`${row.gps_latitude?.toFixed(4)||'-'}, ${row.gps_longitude?.toFixed(4)||'-'}`; return `
        <tr>
        <td>${formatTime(row.received_at)}</td>
        <td>${row.vehicle_id||'?'}</td>
        <td>${row.temperature!==undefined?row.temperature.toFixed(1):'-'}°C</td>
        <td>${row.humidity!==undefined?row.humidity.toFixed(1):'-'}%</td>
        <td>${row.accel_magnitude!==undefined?row.accel_magnitude.toFixed(2):'-'}</td>
        <td><span class="${row.tamper_flag===1?'anomaly':'normal'}">${row.tamper_flag===1?'BỊ MỞ':'AN TOÀN'}</span></td>
        <td style="font-size:11px">${gps}</td>
        <td>${row.snr!==undefined?row.snr.toFixed(0):'-'}</td>
        <td>${row.rssi||'-'}</td></tr>`;}).join('')||'<tr><td colspan="9">Không có dữ liệu</td></tr>'
}
function addAlert(alert){
    const list=document.getElementById('alertsList');
    const div=document.createElement('div');
    div.className='alert';
    let alertText = `[${alert.type}] `;
    
    if (alert.type === 'anomaly_alert') {
        alertText += `Xe ${alert.vehicle_id||alert.node_id||'?'} - Gặp nguy hiểm (Score: ${(alert.nn_anomaly_score||0).toFixed(3)})`;
    } else if (alert.type === 'tamper_alert') {
        alertText += `Xe ${alert.vehicle_id||alert.node_id||'?'} - Phát hiện xâm nhập`;
    } else {
        alertText += `${alert.alert_reason||'Cảnh báo'} - Xe ${alert.vehicle_id||alert.node_id||'?'}`;
    }
    
    div.textContent = alertText;
    list.insertBefore(div,list.firstChild);
    if(list.children.length>5)list.removeChild(list.lastChild);
}
ws.onopen=()=>{console.log('[WS] ✅ Kết nối');updateWebSocketStatus(true)};
ws.onerror=(e)=>{console.error('[WS] ❌ Lỗi:', e);updateWebSocketStatus(false)};
ws.onclose=(e)=>{console.log('[WS] 🔴 Đóng:', e.code, e.reason);updateWebSocketStatus(false)};
ws.onmessage=(e)=>{
    try{
        const msg=JSON.parse(e.data);
        console.log('[WS-RX]', msg.type, msg);
        if(msg.type==='telemetry'){
            console.log('[WS] Telemetry - xe:', msg.vehicle_id, 'anomaly_score:', msg.nn_anomaly_score, 'anomaly_flag:', msg.anomaly_flag, 'tamper_flag:', msg.tamper_flag);
            addTelemetry(msg);
        }
        else if(msg.type==='anomaly_alert'){
            console.log('[WS] ⚠️ Anomaly Alert:', msg.vehicle_id, 'Score:', msg.nn_anomaly_score);
            addAlert(msg);
        }
        else if(msg.type==='tamper_alert'){
            console.log('[WS] ⚠️ Tamper Alert:', msg.vehicle_id);
            addAlert(msg);
        }
        else if(msg.type==='gateway_status'){
            console.log('[WS] Gateway status:', msg);
            document.getElementById('gwId').textContent=msg.gateway_id||'-';
            document.getElementById('heap').textContent=(msg.free_heap/1024).toFixed(1)+' KB';
            document.getElementById('rssi').textContent=(msg.rssi||'-')+' dBm';
            document.getElementById('uptime').textContent=(msg.uptime_ms/1000).toFixed(0)+' s';
        }
    }catch(e){console.error('[WS] Lỗi parse:',e,' data:',e.data)}
};
console.log('[Dashboard] Khởi tạo');
</script></body></html>
)rawliteral";

AsyncWebServer* WebServer::server = nullptr;
uint16_t WebServer::serverPort = 80;

void WebServer::init(uint16_t port) {
    serverPort = port;
    server = new AsyncWebServer(port);
    
    // GET / - Auto-detect: AP mode → config UI, STA mode → dashboard
    server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Check WiFi state
        if (WiFiManager::isInAPMode()) {
            // AP mode: Show WiFi configuration UI
            request->send(200, "text/html", index_html);
        } else {
            // STA mode: Show dashboard
            request->send(200, "text/html", dashboard_html);
        }
    });
    
    // GET /config - WiFi configuration page  
    server->on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", index_html);
    });
    
    // GET /index - Same as /config
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

AsyncWebServer* WebServer::getServer() {
    return server;
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
