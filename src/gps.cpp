#include "gps.h"

static const int GPS_RX_PIN = 16;
static const int GPS_TX_PIN = 17;
static const long GPS_BAUD = 9600;

static double gpsLatitude = 0.0;
static double gpsLongitude = 0.0;
static bool gpsHasFix = false;
static String gpsStatusText = "NO_FIX";

static double parseNmeaCoordinate(const String& field, const String& hemisphere) {
    if (field.length() < 6) {
        return 0.0;
    }

    int dotIndex = field.indexOf('.');
    if (dotIndex < 0) {
        return 0.0;
    }

    int degreesLength = (hemisphere == "N" || hemisphere == "S") ? 2 : 3;
    String degStr = field.substring(0, degreesLength);
    String minStr = field.substring(degreesLength);

    double degrees = degStr.toDouble();
    double minutes = minStr.toDouble();
    double coord = degrees + minutes / 60.0;
    if (hemisphere == "S" || hemisphere == "W") {
        coord = -coord;
    }
    return coord;
}

static void parseGgaSentence(const String& sentence) {
    int parts = 0;
    int start = 0;
    String fields[15];

    for (int i = 0; i < sentence.length() && parts < 15; ++i) {
        if (sentence[i] == ',' || sentence[i] == '*') {
            fields[parts++] = sentence.substring(start, i);
            start = i + 1;
        }
    }

    if (parts < 7) {
        return;
    }

    gpsLatitude = parseNmeaCoordinate(fields[2], fields[3]);
    gpsLongitude = parseNmeaCoordinate(fields[4], fields[5]);
    gpsHasFix = fields[6].toInt() > 0;
    gpsStatusText = gpsHasFix ? "FIX" : "NO_FIX";
}

static void parseRmcSentence(const String& sentence) {
    int parts = 0;
    int start = 0;
    String fields[12];

    for (int i = 0; i < sentence.length() && parts < 12; ++i) {
        if (sentence[i] == ',' || sentence[i] == '*') {
            fields[parts++] = sentence.substring(start, i);
            start = i + 1;
        }
    }

    if (parts < 7) {
        return;
    }

    if (fields[2] != "A") {
        gpsHasFix = false;
        gpsStatusText = "NO_FIX";
        return;
    }

    gpsLatitude = parseNmeaCoordinate(fields[3], fields[4]);
    gpsLongitude = parseNmeaCoordinate(fields[5], fields[6]);
    gpsHasFix = true;
    gpsStatusText = "FIX";
}

bool initGps() {
    Serial2.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    delay(100);
    return true;
}

void updateGps() {
    while (Serial2.available()) {
        String line = Serial2.readStringUntil('\n');
        line.trim();
        if (line.startsWith("$GPGGA")) {
            parseGgaSentence(line);
        } else if (line.startsWith("$GPRMC")) {
            parseRmcSentence(line);
        }
    }
}

bool hasGpsFix() {
    return gpsHasFix;
}

double getGpsLatitude() {
    return gpsLatitude;
}

double getGpsLongitude() {
    return gpsLongitude;
}

String getGpsStatus() {
    return gpsStatusText;
}
