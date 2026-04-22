#ifndef SERIAL_READER_H
#define SERIAL_READER_H

#include <Arduino.h>
#include <String.h>

extern HardwareSerial serialRX;

struct RXPacket {
    uint16_t node_id;          // Node ID (218, 201, etc)
    uint32_t sequence;         // Sequence number
    uint16_t length;           // Payload length
    int16_t rssi;              // Signal strength (dBm)
    int8_t snr;                // Signal-to-Noise ratio
    String payload;            // Actual JSON payload
    uint32_t received_at_ms;   // Timestamp when received
    
    RXPacket() : node_id(0), sequence(0), length(0), 
                 rssi(0), snr(0), received_at_ms(0) {}
};

void initSerialReader();

bool readRXLine(String& line);

bool parseRXLine(String line, RXPacket& packet);

String extractPayload(String line);

bool validateRXPacket(const RXPacket& packet);

void debugPrintRXPacket(const RXPacket& packet);

#endif 
