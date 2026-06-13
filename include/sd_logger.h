#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>
#include <SD.h>
#include <ArduinoJson.h>

/**
 * @file sd_logger.h
 * @brief Module lưu trữ dữ liệu trên thẻ MicroSD (Hộp đen)
 * 
 * Module này quản lý việc ghi nhật ký vận chuyển lên thẻ MicroSD theo định dạng CSV
 * và JSON riêng biệt để hỗ trợ cả live-view và forensic analysis.
 * 
 * Tiêu chuẩn lưu trữ:
 * - /logs/telemetry_YYYYMMDD.csv - Dữ liệu cảm biến hàng ngày
 * - /logs/events_YYYYMMDD.json - Sự kiện quan trọng (tamper, anomaly)
 * - /logs/status_log.txt - Nhật ký trạng thái hệ thống
 * 
 * Ghi nối tiếp và tự động xoay log khi ngày thay đổi.
 */

class SDLogger {
public:
    /// Trạng thái SD card
    enum SDStatus {
        SD_NOT_INITIALIZED,  // Chưa khởi tạo
        SD_MOUNTED,          // Gắn thành công
        SD_ERROR,            // Lỗi hoặc bị tháo
        SD_FULL              // Thẻ nhớ đầy
    };
    
    /// Loại log entry
    enum LogType {
        LOG_TELEMETRY,  // Dữ liệu cảm biến thường xuyên
        LOG_EVENT,      // Sự kiện quan trọng (tamper, anomaly)
        LOG_STATUS,     // Trạng thái hệ thống
        LOG_ERROR       // Lỗi hệ thống
    };
    
    /**
     * @brief Khởi tạo SD card logger
     * @param cs_pin GPIO pin để Chip Select (mặc định: GPIO5)
     * @return true nếu khởi tạo thành công
     * 
     * @note Kết nối SPI:
     * - GPIO18 (SCK) → MicroSD CLK
     * - GPIO23 (MOSI) → MicroSD DI (MOSI)
     * - GPIO19 (MISO) → MicroSD DO (MISO)
     * - GPIO5 (CS) → MicroSD CS
     * - GND → GND, 3V3 → VCC
     */
    static bool init(uint8_t cs_pin = 5);
    
    /**
     * @brief Lưu dữ liệu telemetry vào CSV
     * @param telemetryJson Chuỗi JSON chứa dữ liệu cảm biến
     * @return true nếu ghi thành công
     * 
     * Định dạng CSV:
     * timestamp,device_id,node_id,temperature,humidity,accel_mag,lat,lon,alt,
     * tamper_flag,anomaly_flag,anomaly_score,rssi,snr,sequence
     */
    static bool logTelemetry(const String& telemetryJson);
    
    /**
     * @brief Lưu sự kiện quan trọng vào JSON file
     * @param eventType Loại sự kiện (TAMPER, ANOMALY, v.v.)
     * @param nodeId Node ID gửi sự kiện
     * @param detail JSON chứa chi tiết sự kiện
     * @return true nếu ghi thành công
     */
    static bool logEvent(const String& eventType, uint16_t nodeId, const String& detail);
    
    /**
     * @brief Lưu thông tin trạng thái hệ thống
     * @param message Thông điệp trạng thái
     * @param severity Độ nghiêm trọng (INFO, WARN, ERROR)
     * @return true nếu ghi thành công
     */
    static bool logStatus(const String& message, const String& severity = "INFO");
    
    /**
     * @brief Lấy trạng thái SD card hiện tại
     * @return Enum trạng thái SD
     */
    static SDStatus getStatus();
    
    /**
     * @brief Lấy khoảng trống của SD card (MB)
     * @return Khoảng trống (MB), -1 nếu lỗi
     */
    static int32_t getFreeSpace_MB();
    
    /**
     * @brief Lấy tổng dung lượng SD card (MB)
     * @return Tổng dung lượng (MB), -1 nếu lỗi
     */
    static int32_t getTotalSpace_MB();
    
    /**
     * @brief Xóa nhật ký cũ (hơn N ngày)
     * @param daysOld Xóa log cũ hơn N ngày
     * @return Số file đã xóa
     * @note Gọi định kỳ từ setup() hoặc maintenance task
     */
    static uint16_t cleanOldLogs(uint8_t daysOld = 30);
    
    /**
     * @brief Kiểm tra dung lượng SD card và chuyển sang chế độ cảnh báo nếu cần
     * @return true nếu SD card có dung lượng ok
     */
    static bool checkDiskSpace();
    
    /**
     * @brief Đồng bộ/flush buffer từ bộ nhớ vào SD card
     * @note Nên gọi trước khi restart hoặc power loss
     */
    static void flush();
    
    /**
     * @brief Tắt SD card logger (unmount SD)
     */
    static void end();
    
private:
    static SDStatus sdStatus;
    static File logFile;
    static uint32_t lastFlushTime;
    static uint16_t writeCount;
    static bool lowDiskSpaceWarning;
    
    // Helper functions
    static String getCurrentDateString(); // "20260524"
    static String getCurrentTimeString(); // "14:23:45"
    static bool ensureLogsDirectory();
    static bool rotateLogIfNeeded();
};

#endif // SD_LOGGER_H
