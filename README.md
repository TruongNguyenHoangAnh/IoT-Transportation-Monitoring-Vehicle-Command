# RX_ESP32_Firestore - LoRa Data Gateway to Firestore

NodeMCU ESP32 project để nhận dữ liệu từ LoRa RX gateway (ASR6601) và upload lên **Firestore Cloud**.

## 📋 Tóm tắt

```
LoRa RX Gateway (ASR6601)
         ↓ UART
    ESP32 (Node MCU)
         ↓ WiFi
    Firestore Cloud Database
```

**Chức năng chính:**
- Đọc dữ liệu từ serial port (RX gateway output)
- Parse JSON payload từ các sensors ở node TX
- Upload lên Firestore in real-time
- Queue uploads khi WiFi mất
- Track statistics (RSSI, SNR, packet count, etc.)
- LED heartbeat indicator

---

## 🔧 Hardware

### ESP32 Board
- **Board**: NodeMCU-32S (hoặc tương tự)
- **GPIO Pins**:
  - `GPIO3 (RX)` → RX gateway UART output
  - `GPIO1 (TX)` → RX gateway UART input
  - `GPIO2` → LED (optional)
  - `GPIO0` → Reset button (optional)

### Kết nối UART
```
ESP32 RX (GPIO3) ←→ ASR6601 RX Gateway TX
ESP32 TX (GPIO1) ←→ ASR6601 RX Gateway RX
ESP32 GND ←→ ASR6601 GND
```

**Baud Rate**: 115200

---

## 📦 Dependencies

Được định nghĩa trong `platformio.ini`:

```ini
lib_deps =
    bblanchon/ArduinoJson@^6.19.4
    mobizt/Firebase ESP32 Client@^4.4.5
```

**PlatformIO sẽ tự động download những thư viện này khi build.**

---

## 🚀 Setup & Installation

### 1. Chuẩn bị Firebase Project

1. Truy cập [Firebase Console](https://console.firebase.google.com)
2. Tạo project mới (hoặc dùng project hiện tại)
3. **Enable Firestore Database**:
   - Firestore → Create Database
   - Start in **Test mode** (dễ dàng hơn)
   - Choose region (e.g., `asia-southeast1` for Vietnam)

4. **Get Firebase Credentials**:
   - Project Settings → General
   - Copy: `Project ID`, `Web API Key`, `Database URL`
   - Project Settings → Service Accounts
   - Copy: **Email** và **Password** (hoặc tạo new service account)

### 2. Cập nhật Credentials

1. Mở file `src/config.h`
2. Điền WiFi credentials:
   ```cpp
   #define WIFI_SSID "your_ssid_here"
   #define WIFI_PASS "your_password_here"
   ```

3. Mở file `src/firestore_secrets.h` (hoặc tạo nếu chưa có)
4. Điền Firebase credentials:
   ```cpp
   #define FIREBASE_PROJECT_ID "your-project-id"
   #define FIREBASE_API_KEY "your-api-key"
   #define FIREBASE_EMAIL "your-email@gmail.com"
   #define FIREBASE_PASSWORD "your-password"
   ```

5. **Quan trọng: Add `firestore_secrets.h` vào `.gitignore`** (khi commit)
   ```bash
   echo "src/firestore_secrets.h" >> .gitignore
   ```

### 3. Compile & Upload

```bash
# Build project
platformio run

# Upload to ESP32
platformio run --target upload

# Monitor serial output
platformio device monitor
```

Hoặc dùng **VS Code PlatformIO Extension**:
- Click "Upload" button
- Monitor tab sẽ hiện serial output

---

## 📡 UART Protocol từ RX Gateway

**Format dữ liệu từ RX gateway:**

```
[RX OK] node=218, seq=3, len=145, rssi=-27, snr=12
        Payload: {"vehicle_id":"NODE-218","temp":33.8,"hum":56.7,...}
```

**Parser sẽ extract:**
- `node`: Node ID
- `seq`: Sequence number
- `rssi`: Signal strength (dBm)
- `snr`: Signal-to-Noise ratio (dB)
- `Payload`: JSON string

---

## 📊 Firestore Collection Structure

### 1. Vehicles Telemetry

```
vehicles/
├── NODE-218/
│   └── telemetry/ (subcollection)
│       ├── 1710000000000: {...data...}
│       ├── 1710000010000: {...data...}
│       └── ...
└── NODE-201/
    └── telemetry/
        └── ...
```

**Mỗi document chứa:**
```json
{
  "vehicle_id": "NODE-218",
  "timestamp_ms": 1710000000000,
  "temperature": 33.8,
  "humidity": 56.7,
  "accel_magnitude": 1.41,
  "gps": {
    "latitude": 21.0295,
    "longitude": 105.8581
  },
  "light_level": 211,
  "tamper": 0,
  "status": "OK",
  "gateway": {
    "rssi": -27,
    "snr": 12,
    "sequence": 3
  }
}
```

### 2. Statistics (Real-time Status)

```
statistics/
├── NODE-218: {
    "vehicle_id": "NODE-218",
    "last_seen": 1710000000000,
    "total_packets": 42,
    "rssi": -27,
    "snr": 12,
    "status": "OK",
    ...
  }
└── NODE-201: {...}
```

---

## 🔄 Main Loop Flow

```cpp
Loop:
  1. Check WiFi connection (every 10s)
  2. Read serial from RX gateway
  3. Parse RX packet (node, seq, rssi, snr)
  4. Parse JSON payload
  5. Validate telemetry data
  6. Upload → Firestore (or queue if offline)
  7. Sync pending uploads (every 30s)
  8. Update statistics (every 60s)
  9. Blink LED (heartbeat)
```

---

## 🛠️ File Structure

```
RX_ESP32_Firestore/
├── platformio.ini                    # PlatformIO config
├── README.md                         # This file
├── src/
│   ├── main.cpp                      # Main loop + WiFi
│   ├── config.h                      # Configuration constants
│   ├── firestore_secrets.h           # Firebase credentials (⚠️ GIT IGNORE)
│   ├── serial_reader.h/cpp           # Đọc serial từ RX gateway
│   ├── json_parser.h/cpp             # Parse JSON payload
│   └── firestore_client.h/cpp        # Firestore operations
├── include/
│   └── README
└── lib/
    └── README
```

---

## 📝 Configuration

Sửa `src/config.h` để tùy chỉnh:

```cpp
// Serial
#define SERIAL_BAUD 115200
#define SERIAL_RX_PIN 3
#define SERIAL_TX_PIN 1

// WiFi
#define WIFI_SSID "your_ssid"
#define WIFI_PASS "your_password"

// Timing
#define FIREBASE_SYNC_INTERVAL 30000      // 30 seconds
#define STATISTICS_UPDATE_INTERVAL 60000  // 1 minute
#define WIFI_CHECK_INTERVAL 10000         // 10 seconds

// Queue
#define MAX_PENDING_UPLOADS 50  // Buffer for offline

// Debug
#define DEBUG_SERIAL 1
#define DEBUG_JSON_PARSE 1
#define DEBUG_FIRESTORE 1
```

---

## 🔍 Debug Output

Serial monitor output mẫu:

```
[INFO] ================================
[INFO] RX_ESP32_Firestore Starting...
[INFO] ================================
[SERIAL] Serial reader initialized at 115200 baud
[INFO] Connecting to WiFi: MyWiFi
[INFO] WiFi connected!
[INFO] IP address: 192.168.1.100
[FB] Initializing Firebase/Firestore...
[FB] Firebase initialized successfully!

--- Main loop ---
[SERIAL] Received line: [RX OK] node=218, seq=1, len=145, rssi=-27, snr=12
[RX] Parsing line...
[JSON] Parsing payload: {"vehicle_id":"NODE-218","temp":33.8,...}
[JSON] Parse successful - Vehicle: NODE-218
[FB] Uploading telemetry for NODE-218 to: /vehicles/NODE-218/telemetry/1710000000000
[FB] Upload successful!
```

Sử dụng `#define DEBUG_XXX 0` để tắt logs.

---

## 🔐 Security Notes

### Firebase Credentials
- ⚠️ **NEVER commit `firestore_secrets.h` to Git**
- Use `.gitignore`:
  ```
  src/firestore_secrets.h
  ```
- Hoặc dùng environment variables / secure storage

### Firestore Rules
Với **Test Mode**, bất kỳ ai cũng có thể read/write. Khi production:

```javascript
rules_version = '2';
service cloud.firestore {
  match /databases/{database}/documents {
    match /vehicles/{vehicle_id}/telemetry/{document=**} {
      allow read, write: if request.auth != null;
    }
    match /statistics/{vehicle_id} {
      allow read, write: if request.auth != null;
    }
  }
}
```

### Data Validation
- Tất cả inputs được validate trước upload
- Invalid JSON được bỏ qua (logged as error)
- Out-of-range values được warn nhưng accepted

---

## 🚨 Troubleshooting

### WiFi không connect
- Check SSID/password trong `config.h`
- Check ESP32 is close to router
- Monitor logs: `[WiFi connected/disconnected]`

### Firebase không init
- Verify credentials trong `firestore_secrets.h`
- Confirm Firestore Database created
- Check network connectivity (ping google.com)

### Serial data không read
- Check UART connection (RX ↔ TX swap?)
- Verify baud rate: 115200
- Use logic analyzer nếu có

### Uploads queued nhưng không push
- Check `[FB] Firebase not ready` messages
- Check pending queue: `[INFO] Current pending queue size`
- Force sync: press reset button hoặc restart

### Memory issues
- Monitor `[INFO] Free Heap` messages
- Reduce `MAX_PENDING_UPLOADS` nếu cần
- Check for memory leaks trong ArduinoJson

---

## 📊 Monitoring & Testing

### 1. Check Firestore via Web Console
```
Firebase Console → Firestore Database
├── Collection: vehicles
│   └── Docs with telemetry
└── Collection: statistics
    └── Nodes status
```

### 2. Query Recent Data (Firebase Console)
```
Collection: vehicles/NODE-218/telemetry
Order by: timestamp_ms (Descending)
Limit: 10
```

### 3. Emulate Data (for testing)
Write mock [RX OK] messages to ESP32 serial:
```
[RX OK] node=218, seq=1, len=145, rssi=-27, snr=12
        Payload: {"vehicle_id":"NODE-218","temp":33.8,"hum":56.7,"accel_mag":1.41,"gps":{"lat":21.0295,"lng":105.8581},"light_level":211,"tamper":0,"status":"OK"}
```

---

## ✅ Checklist

- [ ] PlatformIO installed
- [ ] Firebase project created + Firestore enabled
- [ ] Credentials filled in `config.h` and `firestore_secrets.h`
- [ ] `.gitignore` updated
- [ ] Code compiled successfully
- [ ] Uploaded to ESP32
- [ ] Serial monitor shows WiFi connected
- [ ] Serial monitor shows Firebase initialized
- [ ] RX gateway sending data over UART
- [ ] Telemetry appearing in Firestore
- [ ] LED blinking (heartbeat)

---

## 📚 References

- [PlatformIO Documentation](https://docs.platformio.org/)
- [Firebase ESP32 Client](https://github.com/mobizt/Firebase-ESP32)
- [ArduinoJson Documentation](https://arduinojson.org/)
- [Firestore Documentation](https://firebase.google.com/docs/firestore)
- [ESP32 GPIO Pinout](https://randomnerdtutorials.com/esp32-pinout-reference-gpios/)

---

## 📞 Support

- Check serial logs with `DEBUG_XXX = 1`
- Verify Firebase credentials
- Check network connectivity
- Review Firestore rules in console

**Happy data logging! 🚀**
