#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

// Global battery variables
extern BatteryFilter batteryFilter;
extern float lastBatteryVoltage;
extern int batteryPercent;

// ===== BATTERY FUNCTIONS =====

/**
 * Median filter - removes spikes from WiFi interference
 * @param data Array of samples
 * @param size Number of samples
 * @return Median value
 */
float medianFilter(int* data, int size);

/**
 * Read battery voltage with triple filtering (median + EMA)
 * @return Filtered battery voltage in volts
 */
float readBatteryVoltage();

/**
 * Map voltage to battery percentage using Li-Ion discharge curve
 * @param v Voltage in volts
 * @return Battery percentage (0-100)
 */
int voltageToPercent(float v);

/**
 * Check if battery is critically low and enter deep sleep if needed
 * @param voltage Current battery voltage
 */
void checkBatteryAndShutdown(float voltage);

#endif // BATTERY_H
