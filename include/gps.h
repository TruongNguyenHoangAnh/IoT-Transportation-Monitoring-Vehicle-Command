#ifndef GPS_H
#define GPS_H

#include <Arduino.h>

bool initGps();
void updateGps();
bool hasGpsFix();
double getGpsLatitude();
double getGpsLongitude();
String getGpsStatus();

#endif // GPS_H
