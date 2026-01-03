#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

// Initialize battery measurement
void batteryInit();

// Read battery percentage (0-100)
int readBatteryPercent();

#endif // BATTERY_H