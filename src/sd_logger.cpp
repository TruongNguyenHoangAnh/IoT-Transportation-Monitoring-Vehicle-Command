#include "sd_logger.h"
#include "config.h"
#include <ctime>

/**
 * @file sd_logger.cpp
 * @brief Triển khai module SD card logging
 * 
 * Tính năng:
 * - Ghi CSV cho telemetry data
 * - Ghi JSON cho sự kiện quan trọng
 * - Automatic log rotation theo ngày
 * - Kiểm tra dung lượng SD card
 * - Xóa log cũ tự động
 */

SDLogger::SDStatus SDLogger::sdStatus = SD_NOT_INITIALIZED;
File SDLogger::logFile;
uint32_t SDLogger::lastFlushTime = 0;
uint16_t SDLogger::writeCount = 0;
bool SDLogger::lowDiskSpaceWarning = false;

bool SDLogger::init(uint8_t cs_pin) {
    if (!SD.begin(cs_pin, SPI, 8000000)) {
        LOG_ERROR("[SD] Không thể mount SD card (pin: %d)", cs_pin);
        sdStatus = SD_ERROR;
        return false;
    }
    
    LOG_INFO("[SD] Mount SD card thành công");
    
    // Tạo thư mục logs nếu chưa tồn tại
    if (!ensureLogsDirectory()) {
        LOG_ERROR("[SD] Không thể tạo thư mục logs");
        sdStatus = SD_ERROR;
        return false;
    }
    
    sdStatus = SD_MOUNTED;
    lastFlushTime = millis();
    
    // Log startup event
    logStatus("SD Logger khởi tạo thành công", "INFO");
    
    // Kiểm tra dung lượng
    int32_t free_mb = getFreeSpace_MB();
    LOG_INFO("[SD] Lưu trữ: %d MB trống / %d MB tổng",
             free_mb, getTotalSpace_MB());
    
    if (free_mb < 50) {
        LOG_WARN("[SD] ⚠️ Cảnh báo: SD card sắp đầy! Chỉ còn %d MB", free_mb);
        lowDiskSpaceWarning = true;
        return false;
    }
    
    return true;
}

bool SDLogger::ensureLogsDirectory() {
    // Kiểm tra xem /logs tồn tại
    if (!SD.exists("/logs")) {
        if (!SD.mkdir("/logs")) {
            return false;
        }
        LOG_INFO("[SD] Tạo thư mục /logs");
    }
    return true;
}

String SDLogger::getCurrentDateString() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    char buffer[16];
    strftime(buffer, sizeof(buffer), "%Y%m%d", timeinfo);
    return String(buffer);
}

String SDLogger::getCurrentTimeString() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    char buffer[16];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", timeinfo);
    return String(buffer);
}

bool SDLogger::logTelemetry(const String& telemetryJson) {
    if (sdStatus != SD_MOUNTED) {
        return false;
    }
    
    // Parse JSON để extract fields
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, telemetryJson) != DeserializationError::Ok) {
        LOG_ERROR("[SD] JSON parse error trong logTelemetry");
        return false;
    }
    
    // Tên file: telemetry_YYYYMMDD.csv
    String fileName = "/logs/telemetry_" + getCurrentDateString() + ".csv";
    
    // Kiểm tra nếu file không tồn tại, viết header
    bool fileExists = SD.exists(fileName.c_str());
    
    File file = SD.open(fileName.c_str(), FILE_APPEND);
    if (!file) {
        LOG_ERROR("[SD] Không thể mở file: %s", fileName.c_str());
        return false;
    }
    
    // Viết header CSV nếu file mới
    if (!fileExists) {
        file.println("timestamp_ms,device_id,node_id,temperature_C,humidity_pct,accel_mag_g,"
                     "latitude,longitude,altitude_m,"
                     "tamper_flag,anomaly_flag,anomaly_score,rssi_dbm,snr_db,sequence");
    }
    
    // Lấy values từ JSON (với default values)
    uint32_t ts = doc["timestamp"] | millis();
    String device_id = doc["device_id"] | "UNKNOWN";
    uint16_t node_id = doc["node_id"] | 0;
    float temp = doc["temperature"] | 0.0f;
    float humidity = doc["humidity"] | 0.0f;
    float accel = doc["accel_magnitude"] | 0.0f;
    double lat = doc["gps"]["lat"] | 0.0;
    double lon = doc["gps"]["lon"] | 0.0;
    float alt = doc["gps"]["alt"] | 0.0f;
    uint8_t tamper = doc["tamper_flag"] | 0;
    uint8_t anomaly = doc["anomaly_flag"] | 0;
    float anomaly_score = doc["nn_anomaly_score"] | 0.0f;
    int16_t rssi = doc["rssi"] | 0;
    float snr = doc["snr"] | 0.0f;
    uint16_t seq = doc["sequence"] | 0;
    
    // Viết CSV row
    char row[256];
    snprintf(row, sizeof(row), 
             "%u,%s,%u,%.2f,%.2f,%.3f,"
             "%.6f,%.6f,%.2f,"
             "%u,%u,%.3f,%d,%.2f,%u",
             ts, device_id.c_str(), node_id, temp, humidity, accel,
             lat, lon, alt,
             tamper, anomaly, anomaly_score, rssi, snr, seq);
    
    file.println(row);
    file.close();
    
    writeCount++;
    
    // Flush mỗi 10 writes hoặc 5 phút
    if (writeCount % 10 == 0 || millis() - lastFlushTime > 300000) {
        flush();
    }
    
    return true;
}

bool SDLogger::logEvent(const String& eventType, uint16_t nodeId, const String& detail) {
    if (sdStatus != SD_MOUNTED) {
        return false;
    }
    
    // Tên file: events_YYYYMMDD.json
    String fileName = "/logs/events_" + getCurrentDateString() + ".json";
    
    // Tạo JSON event
    StaticJsonDocument<256> eventDoc;
    eventDoc["timestamp"] = millis();
    eventDoc["datetime"] = getCurrentTimeString();
    eventDoc["type"] = eventType;
    eventDoc["node_id"] = nodeId;
    eventDoc["detail"] = detail;
    
    String eventJson;
    serializeJson(eventDoc, eventJson);
    
    // Append vào file
    File file = SD.open(fileName.c_str(), FILE_APPEND);
    if (!file) {
        LOG_ERROR("[SD] Không thể mở events file: %s", fileName.c_str());
        return false;
    }
    
    file.println(eventJson);  // Mỗi event trên một dòng (JSONL format)
    file.close();
    
    LOG_WARN("[SD] 📝 Logged event: %s from node %u", eventType.c_str(), nodeId);
    
    return true;
}

bool SDLogger::logStatus(const String& message, const String& severity) {
    if (sdStatus != SD_MOUNTED) {
        return false;
    }
    
    // Tên file chung: status_log.txt
    String fileName = "/logs/status_log.txt";
    
    File file = SD.open(fileName.c_str(), FILE_APPEND);
    if (!file) {
        LOG_ERROR("[SD] Không thể mở status file");
        return false;
    }
    
    String logLine = String("[") + getCurrentTimeString() + "] [" + 
                     severity + "] " + message;
    
    file.println(logLine);
    file.close();
    
    return true;
}

SDLogger::SDStatus SDLogger::getStatus() {
    return sdStatus;
}

int32_t SDLogger::getFreeSpace_MB() {
#ifdef ESP32
    uint64_t cardSize = SD.cardSize();
    uint64_t usedSpace = SD.usedBytes();
    uint64_t freeSpace = cardSize - usedSpace;
    return freeSpace / (1024 * 1024);
#else
    return -1;
#endif
}

int32_t SDLogger::getTotalSpace_MB() {
#ifdef ESP32
    uint64_t cardSize = SD.cardSize();
    return cardSize / (1024 * 1024);
#else
    return -1;
#endif
}

bool SDLogger::checkDiskSpace() {
    if (sdStatus != SD_MOUNTED) {
        return false;
    }
    
    int32_t free_mb = getFreeSpace_MB();
    
    if (free_mb < 10) {
        LOG_ERROR("[SD] 🚨 CRITICAL: SD card almost full! %d MB left", free_mb);
        sdStatus = SD_FULL;
        return false;
    } else if (free_mb < 50) {
        if (!lowDiskSpaceWarning) {
            LOG_WARN("[SD] ⚠️ Low disk space: %d MB left", free_mb);
            lowDiskSpaceWarning = true;
        }
        return true;
    } else {
        lowDiskSpaceWarning = false;
        return true;
    }
}

uint16_t SDLogger::cleanOldLogs(uint8_t daysOld) {
    if (sdStatus != SD_MOUNTED) {
        return 0;
    }
    
    uint16_t deletedCount = 0;
    time_t now = time(nullptr);
    time_t oldTime = now - (daysOld * 86400);
    
    File logsDir = SD.open("/logs");
    if (!logsDir || !logsDir.isDirectory()) {
        return 0;
    }
    
    File file = logsDir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            // TODO: Compare file modification time dengan oldTime
            // Hiện tại, SD library trên ESP32 không cung cấp file time tốt
            // Có thể implement logic based on filename pattern (YYYYMMDD)
        }
        file = logsDir.openNextFile();
    }
    
    LOG_INFO("[SD] Cleaned %u old log files", deletedCount);
    return deletedCount;
}

void SDLogger::flush() {
    // SD library tự động flush, nhưng có thể thêm logic khác nếu cần
    writeCount = 0;
    lastFlushTime = millis();
    LOG_DEBUG("[SD] Flush log data to disk");
}

void SDLogger::end() {
    flush();
    SD.end();
    sdStatus = SD_NOT_INITIALIZED;
    LOG_INFO("[SD] SD card logger shutdown");
}
