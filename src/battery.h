#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

// Inicjalizacja pomiaru baterii
// - Konfiguruje ADC (12-bitowy, kanał zgodny z BATTERY_ADC_PIN)
// - Kalibruje napięcie ADC z użyciem esp_adc_cal_characterize
// - Przygotowuje moduł do dokładnych pomiarów napięcia baterii
void batteryInit();

// Odczyt poziomu baterii w procentach (0-100%)
// - Uśrednia kilka pomiarów ADC w celu stabilności odczytu
// - Uwzględnia dzielnik napięcia (BATTERY_DIVIDER_RATIO)
// - Mapuje napięcie na procenty od BATTERY_MIN_MV do BATTERY_MAX_MV
// - Zwraca przybliżony stan naładowania baterii
int readBatteryPercent();

#endif // BATTERY_H