#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

// Inicjalizacja pomiaru baterii
// Ustawia ADC, kalibruje napięcie i przygotowuje moduł do pomiarów
void batteryInit();

// Odczyt poziomu baterii w procentach (0-100%)
// Zwraca przybliżony stan naładowania baterii w %
int readBatteryPercent();

#endif // BATTERY_H