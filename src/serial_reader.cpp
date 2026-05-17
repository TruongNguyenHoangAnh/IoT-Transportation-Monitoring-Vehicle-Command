#include "serial_reader.h"
#include "config.h"

HardwareSerial serialRX(1);

void initSerialReader() {
    // Initialize UART1 on GPIO33 (RX/D9) and GPIO32 (TX/D10)
    serialRX.begin(SERIAL_BAUD, SERIAL_8N1, SERIAL_RX_PIN, SERIAL_TX_PIN);
    delay(500);
    LOG_SERIAL("UART1 reader initialized at %d baud (RX=GPIO%d/D9, TX=GPIO%d/D10)", SERIAL_BAUD, SERIAL_RX_PIN, SERIAL_TX_PIN);
}

static bool parseFieldInt(const String& line, const char* name, int offset, int& output, char delimiter = ',') {
    int start = line.indexOf(name);
    if (start == -1) return false;
    start += offset;
    int end = line.indexOf(delimiter, start);
    if (end == -1) end = line.length();
    String value = line.substring(start, end);
    value.trim();
    if (value.length() == 0) return false;
    output = value.toInt();
    return true;
}

static bool lineLooksLikeBase64(const String& line) {
    String trimmed = line;
    trimmed.trim();
    if (trimmed.length() == 0 || trimmed.length() % 4 != 0) {
        return false;
    }
    for (size_t i = 0; i < trimmed.length(); ++i) {
        char c = trimmed[i];
        if (!(isalnum(c) || c == '+' || c == '/' || c == '=')) {
            return false;
        }
    }
    return true;
}

bool readRXLine(String& line) {
    static uint32_t lastIgnoredLogTime = 0;

    while (serialRX.available() > 0) {
        String firstLine = serialRX.readStringUntil('\n');
        firstLine.trim();
        if (firstLine.length() == 0) {
            continue;
        }

        if (firstLine.startsWith("[RX OK]")) {
            unsigned long startTime = millis();
            String payloadLine;
            while (millis() - startTime < 3000) {
                if (serialRX.available() > 0) {
                    payloadLine = serialRX.readStringUntil('\n');
                    payloadLine.trim();
                    if (payloadLine.length() > 0) {
                        line = firstLine + " " + payloadLine;
                        return true;
                    }
                }
                delay(5);
            }
            // Incomplete packet: drop it rather than return partial metadata.
            continue;
        }

        if (firstLine.startsWith("[TEST]")) {
            line = firstLine;
            return true;
        }

        if (firstLine.startsWith("{") || lineLooksLikeBase64(firstLine)) {
            line = firstLine;
            return true;
        }

        // Log ignored lines periodically
        if (millis() - lastIgnoredLogTime > 10000) {
            LOG_INFO("[UART1] ignored serial line: %s", firstLine.c_str());
            lastIgnoredLogTime = millis();
        }
    }
    return false;
}

bool parseRXLine(String line, RXPacket& packet) {
    if (!line.startsWith("[RX OK]")) {
        return false;
    }

    int value;
    if (!parseFieldInt(line, "node=", 5, value)) return false;
    packet.node_id = value;
    if (!parseFieldInt(line, "seq=", 4, value)) return false;
    packet.sequence = value;
    if (!parseFieldInt(line, "len=", 4, value)) return false;
    packet.length = value;
    if (!parseFieldInt(line, "rssi=", 5, value)) return false;
    packet.rssi = value;
    if (!parseFieldInt(line, "snr=", 4, value, ' ')) return false;
    packet.snr = value;

    packet.payload = extractPayload(line);
    packet.payload.trim();
    packet.received_at_ms = millis();
    return true;
}

String extractPayload(String line) {
    int payloadIdx = line.indexOf("Payload:");
    if (payloadIdx == -1) {
        return "";
    }
    return line.substring(payloadIdx + 9);
}

bool validateRXPacket(const RXPacket& packet) {
    // Basic validation
    if (packet.node_id == 0) {
        LOG_ERROR("Invalid node_id: 0");
        return false;
    }
    
    if (packet.payload.length() == 0) {
        LOG_ERROR("Empty payload");
        return false;
    }
    
    if (!(packet.payload.startsWith("{") || packet.payload.startsWith("H"))) {
        LOG_ERROR("Payload doesn't look like JSON or HEARTBEAT");
        return false;
    }
    
    return true;
}

void debugPrintRXPacket(const RXPacket& packet) {
    LOG_INFO("=== RX Packet ===");
    LOG_INFO("Node ID: %d", packet.node_id);
    LOG_INFO("Sequence: %d", packet.sequence);
    LOG_INFO("Length: %d", packet.length);
    LOG_INFO("RSSI: %d dBm", packet.rssi);
    LOG_INFO("SNR: %d dB", packet.snr);
    LOG_INFO("Payload: %s", packet.payload.c_str());
    LOG_INFO("Received at: %d ms", packet.received_at_ms);
    LOG_INFO("=================");
}
