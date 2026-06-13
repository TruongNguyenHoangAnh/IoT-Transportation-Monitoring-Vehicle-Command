// ============================================================================
// mqtt_tls_ca_cert.h
// 
// Self-Signed CA Certificate embedded in ESP32 firmware
// This file is auto-generated - DO NOT EDIT MANUALLY
// 
// Usage:
//   1. Run: openssl x509 -in ca.crt -outform PEM | base64
//   2. Paste the base64 output below
//   3. Include this header in your firmware
//   4. Use: mqttClient.setCACert(CA_CERT_PEM)
//
// ============================================================================

#ifndef MQTT_TLS_CA_CERT_H
#define MQTT_TLS_CA_CERT_H

// ============================================================================
// METHOD 1: Embed PEM Certificate Directly (Recommended)
// ============================================================================
// This is the CA certificate in PEM format (text-based)
// Copy-paste from generate_mqtt_tls_certs.sh output

static const char CA_CERT_PEM[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDkTCCAnmgAwIBAgIUYSjhOpWG5wW/RnMtvIKnTn3OqmEwDQYJKoZIhvcNAQEL
BQAwWDELMAkGA1UEBhMCVk4xDDAKBgNVBAgMA0hDTTESMBAGA1UEBwwJSG9DaGlN
aW5oMRMwEQYDVQQKDApFZGdlQUktSW9UMRIwEAYDVQQDDAlFZGdlQUktQ0EwHhcN
MjYwNTI2MTA1ODUyWhcNMzYwNTIzMTA1ODUyWjBYMQswCQYDVQQGEwJWTjEMMAoG
A1UECAwDSENNMRIwEAYDVQQHDAlIb0NoaU1pbmgxEzARBgNVBAoMCkVkZ2VBSS1J
b1QxEjAQBgNVBAMMCUVkZ2VBSS1DQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCC
AQoCggEBANJ0G9idcWlsp3+ZbcgAnEuoihaQuqmgwa1BHwB2NqKi6FwfUgT2iFvl
IT9gEQzNzL4ZNsuAyHpLSNfs1IRrYYhqKK9AILCTtZkO4fYJ/zd9yO8tkTBjZ1QX
/ebKo3FLZ8qggb4UdxZ2xpn6RlnFY5MjhXmD+gM3CeCrAM98rw0AdJ+z1hbxVi1Z
MMY4jIhEOIwZ2jVyUXWU50lSqm6Uh2EwxhYOqbYydgQNFOuEK4ynfq/Aihp0pZ5C
q5KDnsfSnyZgqxBBaHiSiCCZoD9kny26gC1ElDOGiDJyqUEv2Hto8uSgJOiguUU+
uNx3tnhc18CI6afQxBFpOgztW5cXkxkCAwEAAaNTMFEwHQYDVR0OBBYEFMeQzUoX
gfYum2HtB4OpDajHeygaMB8GA1UdIwQYMBaAFMeQzUoXgfYum2HtB4OpDajHeyga
MA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEBAHXKySZgMSwyqgd4
eVc/NqA75ggBkAMU/U4M4vt4NTGBxYzqO7CKdk9bdxXXwJZvWo3mRskEXACBhBJF
/b9t009lCWOqENfxgQaKD6TY6f18NTNQ2T1Q2bY93yUPYobvLC4wDWGZx99XU50Q
njqG1ikpg+0dCzCSkCn2YIvw4X2gaR0+e9wjSQymkVaJaeC6kgBNRyVWVVGwuNN6
mDrViOupuXNw5aRCO/7uFmVuTkUgDh/B5kmQLw97tFEt6a7lpbToHV15pojChTz2
bNVUKSrm5Ydm+YvatW0oXoGLtpamEDSGR5qjEEIE7+fFGYgygs3VCZV837+lka6p
X8vdIWU=
-----END CERTIFICATE-----
)EOF";

// ============================================================================
// METHOD 2: Store Certificate from SPIFFS (Alternative)
// ============================================================================
// If you prefer to load from SD card / Flash instead:
// 
// SPIFFS circuit diagram:
//   ESP32 SPIFFS: /ca.crt
//   Code: File caCertFile = SPIFFS.open("/ca.crt", "r");
//   
// Advantages:
//   - Can update cert without recompiling
//   - Saves flash space (15KB+ per 2048-bit cert)
// Disadvantages:
//   - Depends on SPIFFS availability
//   - Slower to load at startup

/*
// Alternative: Load from SPIFFS
void loadCertFromSPIFFS() {
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed!");
        return;
    }
    
    File f = SPIFFS.open("/ca.crt", "r");
    if (!f) {
        Serial.println("ca.crt not found in SPIFFS!");
        return;
    }
    
    size_t size = f.size();
    char* buffer = new char[size + 1];
    f.readBytes(buffer, size);
    buffer[size] = '\0';
    f.close();
    
    // Use with WiFiClientSecure
    secureClient.setCACert(buffer);
    
    delete[] buffer;
}
*/

// ============================================================================
// HELPER FUNCTION: Validate Certificate
// ============================================================================
// Quick validation of embedded certificate

#include <string.h>

inline bool validateCertificate() {
    // Check if certificate starts with PEM magic bytes
    if (strstr(CA_CERT_PEM, "-----BEGIN CERTIFICATE-----") == nullptr) {
        return false;
    }
    if (strstr(CA_CERT_PEM, "-----END CERTIFICATE-----") == nullptr) {
        return false;
    }
    return true;
}

// ============================================================================
// USAGE EXAMPLE IN MAIN CODE
// ============================================================================
/*

#include "mqtt_tls_ca_cert.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);

void setupMQTTSecure() {
    // Set MQTT broker address
    const char* MQTT_HOST = "mqtt.edgeai.local";  // OR 192.168.1.100
    const int MQTT_PORT = 8883;
    
    // Configure secure connection
    if (!validateCertificate()) {
        Serial.println("CA certificate validation failed!");
        return;
    }
    
    secureClient.setCACert(CA_CERT_PEM);
    secureClient.setServerName(MQTT_HOST);  // Verify CN in certificate
    
    // Optional: Set insecure (ONLY for debugging!) ❌ DO NOT USE IN PRODUCTION
    // secureClient.setInsecure();
    
    // Setup MQTT
    mqttClient.setClient(secureClient);
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    
    // Connect
    if (mqttClient.connect("esp32_gateway")) {
        Serial.println("MQTT over TLS connected!");
    } else {
        Serial.println("MQTT connection failed!");
    }
}

*/

#endif // MQTT_TLS_CA_CERT_H
