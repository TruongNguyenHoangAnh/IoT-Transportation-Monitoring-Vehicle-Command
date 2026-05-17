#ifndef CONFIG_H
#define CONFIG_H

#define SERIAL_BAUD 115200
#define SERIAL_RX_PIN 33     // RX from RX gateway (GPIO33/D9)
#define SERIAL_TX_PIN 32     // TX to RX gateway (GPIO32/D10)

#define RATE_LIMIT_ATTEMPTS 3
#define RATE_LIMIT_WINDOW 300000  

#define USE_WIFI_MANAGER 1              
#define WIFI_MANAGER_PORTAL_TIMEOUT 300 

// #define WIFI_SSID "TESP32"
// #define WIFI_PASS "11112222"
#define WIFI_AP_SSID "ESP32-TRANSPORT"
#define WIFI_AP_PASSWORD "12345678"
#define WIFI_AP_IP 192, 168, 4, 1
#define WIFI_AP_GATEWAY 192, 168, 4, 1
#define WIFI_AP_SUBNET 255, 255, 255, 0
#define WIFI_AP_PORT 80

// STA Mode Configuration
#define WIFI_CONNECT_TIMEOUT_MS 20000   // 20 seconds
#define WIFI_AP_TIMEOUT_MS 120000       // 120 seconds (config mode timeout)
#define WIFI_RECONNECT_ATTEMPTS 3
#define WIFI_SCAN_TIMEOUT_MS 10000

#define CONFIG_BUTTON_PIN 25
#define CONFIG_BUTTON_HOLD_MS 5000      // 5 seconds to trigger AP mode

#define FIREBASE_PROJECT_ID "iot-transportation-monitoring"
#define FIREBASE_API_KEY "AIzaSyABLZpPVFhE-Fo7Khdq-WDhmchUiyotTCE"
#define FIREBASE_EMAIL "truongnguyenhoanganh0208@gmail.com"
#define FIREBASE_PASSWORD "0947740208"
#define FIREBASE_DATABASE_URL "https://iot-transportation-monitoring-default-rtdb.asia-southeast1.firebasedatabase.app"

// ESP32 GPIO PINS
#define LED_PIN 2

// TIMING CONSTANTS
#define WIFI_CHECK_INTERVAL 10000
constexpr uint32_t NODE_STATUS_INTERVAL = 60000;
constexpr uint32_t VEHICLE_OFFLINE_TIMEOUT_MS = 120000;
constexpr uint32_t FEATURE_WINDOW_MS = 5000;
constexpr uint32_t FEATURE_LOG_INTERVAL_MS = 5000;
constexpr uint32_t GATEWAY_GPS_STATUS_INTERVAL = 5000;

#define MQTT_BROKER "10.77.190.75"
#define MQTT_PORT 1883               
#define MQTT_CLIENT_ID "ESP32_RX_Gateway"
#define MQTT_TOPIC_PREFIX "vehicles"

// MQTT auth
#define MQTT_USERNAME "mqttuser"
#define MQTT_PASSWORD "mqttpassword"
#define MQTT_USE_TLS false             
#define MQTT_TLS_STRICT false

// HMAC authentication for payload integrity (Transport nodes to RX gateway)
#define HMAC_SECRET "datn_252_secret_key"

#define DEBUG_SERIAL 0        // Disable detailed UART logs
#define DEBUG_JSON_PARSE 0    // Disable JSON parse logs (only show errors)
#define DEBUG_FIRESTORE 0     // Disable FB logs
#define DEBUG_MQTT 1          // Keep MQTT debug for troubleshooting

#if DEBUG_SERIAL
  #define LOG_SERIAL(fmt, ...) Serial.printf("[SERIAL] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG_SERIAL(...)
#endif

#if DEBUG_JSON_PARSE
  #define LOG_JSON(fmt, ...) Serial.printf("[JSON] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG_JSON(...)
#endif

#if DEBUG_FIRESTORE
  #define LOG_FB(fmt, ...) Serial.printf("[FB] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG_FB(...)
#endif

#if DEBUG_MQTT
  #define LOG_MQTT(fmt, ...) Serial.printf("[MQTT] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG_MQTT(...)
#endif

#define LOG_INFO(fmt, ...) Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_ALERT(fmt, ...) Serial.printf("[ALERT] " fmt "\n", ##__VA_ARGS__)

#endif // CONFIG_H
