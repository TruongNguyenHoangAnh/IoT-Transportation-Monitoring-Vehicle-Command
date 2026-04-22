#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <Arduino.h>

// Forward declaration
struct RXPacket;

/**
 * Generate HMAC-SHA256 signature
 * @param data Input data to sign
 * @param length Length of data
 * @param outputHex Output hex string (65 bytes, null-terminated)
 * @return true if successful
 */
bool hmacSha256(const char *data, size_t length, char outputHex[65]);

/**
 * HMAC-SHA256 overload for String
 */
inline bool hmacSha256(const String &message, char outputHex[65]) {
    return hmacSha256(message.c_str(), message.length(), outputHex);
}

/**
 * Decrypt AES-CBC encrypted payload (base64 encoded)
 * Returns true if decryption successful or payload already plaintext JSON
 * @param rxPacket RX packet to decrypt (modifies payload in place)
 * @return true if decryption successful or already plaintext
 */
bool decryptPayload(RXPacket& rxPacket);

#endif 