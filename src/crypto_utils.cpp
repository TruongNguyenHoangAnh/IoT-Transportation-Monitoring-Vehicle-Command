#include "crypto_utils.h"
#include "config.h"
#include "serial_reader.h"
#include "json_parser.h"
#include "mbedtls/md.h"
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"

bool hmacSha256(const char *data, size_t length, char outputHex[65]) {
    if (outputHex == nullptr || data == nullptr) {
        return false;
    }

    unsigned char output[32];
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char*)HMAC_SECRET, strlen(HMAC_SECRET));
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)data, length);
    mbedtls_md_hmac_finish(&ctx, output);
    mbedtls_md_free(&ctx);

    static const char hexDigits[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        unsigned char value = output[i];
        outputHex[i * 2] = hexDigits[(value >> 4) & 0x0F];
        outputHex[i * 2 + 1] = hexDigits[value & 0x0F];
    }
    outputHex[64] = '\0';
    return true;
}

/**
 * Decrypt AES-CBC encrypted payload (base64 encoded)
 * Returns true if decryption successful or payload already plaintext JSON
 */
bool decryptPayload(RXPacket& rxPacket) {
    if (rxPacket.payload.startsWith("{")) {
        return true;  // Already plaintext JSON
    }

    String encrypted_base64 = rxPacket.payload;
    
    // Hardening: Check if base64 is valid before attempting decode
    if (encrypted_base64.length() == 0 || (encrypted_base64.length() % 4) != 0) {
        LOG_ERROR("Invalid base64 length for encryption: %u bytes", (unsigned int)encrypted_base64.length());
        return false;
    }
    
    unsigned char enc_buf[512];
    size_t enc_len = 0;
    int b64_ret = mbedtls_base64_decode(enc_buf, sizeof(enc_buf), &enc_len,
                                        (const unsigned char*)encrypted_base64.c_str(),
                                        encrypted_base64.length());
    if (b64_ret != 0) {
        LOG_ERROR("Base64 decode failed (ret=%d)", b64_ret);
        return false;
    }
    if (enc_len == 0) {
        LOG_ERROR("Base64 decode produced 0 bytes");
        return false;
    }
    if (enc_len % 16 != 0) {
        LOG_ERROR("Invalid AES block size (len=%u) - not multiple of 16", (unsigned int)enc_len);
        return false;
    }

    static const unsigned char aes_key[16] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10
    };
    static const unsigned char aes_iv[16] = {
        0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0x07,0x18,0x29,0x3A,0x4B,0x5C,0x6D,0x7E,0x8F,0x90
    };

    unsigned char dec_buf[512];
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, aes_key, 128);
    unsigned char iv[16];
    memcpy(iv, aes_iv, 16);
    int ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, enc_len, iv, enc_buf, dec_buf);
    mbedtls_aes_free(&aes);
    if (ret != 0) {
        LOG_ERROR("AES decrypt failed: %d", ret);
        return false;
    }

    size_t pad = dec_buf[enc_len - 1];
    if (pad > 16) pad = 0;
    size_t json_len = enc_len - pad;
    dec_buf[json_len] = '\0';
    rxPacket.payload = String((char*)dec_buf);
    return true;
}