#include "battery.h"
#include "display.h"

// Global battery variables
BatteryFilter batteryFilter;
float lastBatteryVoltage = 3.7;
int batteryPercent = 100;

// Forward declaration
void enterDeepSleep();

// Median filter - usuwa spike'i od WiFi
float medianFilter(int* data, int size) {
  // Bubble sort (wystarczający dla 30 próbek)
  for (int i = 0; i < size - 1; i++) {
    for (int j = 0; j < size - i - 1; j++) {
      if (data[j] > data[j + 1]) {
        int temp = data[j];
        data[j] = data[j + 1];
        data[j + 1] = temp;
      }
    }
  }
  // Zwróć medianę
  if (size % 2 == 0) {
    return (data[size / 2 - 1] + data[size / 2]) / 2.0;
  } else {
    return data[size / 2];
  }
}

// Odczyt napięcia baterii z triple filtering
float readBatteryVoltage() {
  int samples[BATTERY_SAMPLES];
  
  // Pobierz próbki
  for (int i = 0; i < BATTERY_SAMPLES; i++) {
    samples[i] = analogRead(BATTERY_PIN);
    delayMicroseconds(500);  // Krótka przerwa między próbkami
  }
  
  // Median filter
  float medianValue = medianFilter(samples, BATTERY_SAMPLES);
  
  // Konwersja ADC → napięcie (dzielnik 1:1, zakres 0-3.3V dla ADC2)
  // ADC 12-bit: 0-4095, voltage divider 1:1 (100k:100k) = Vbat/2
  float voltage = (medianValue / 4095.0) * 3.3 * 2.0;
  
  // Korekcja kalibracyjna
  voltage *= ADC_CORRECTION_FACTOR;
  
  // EMA filter
  voltage = batteryFilter.update(voltage);
  
  return voltage;
}

// Mapowanie voltage → procent (nieliniowa krzywa Li-Ion)
int voltageToPercent(float v) {
  // 19-punktowa krzywa rozładowania Li-Ion (z ulepszonymi punktami w zakresie krytycznym)
  const float voltages[] = {4.20, 4.15, 4.11, 4.08, 4.02, 3.98, 3.95, 3.91, 3.87, 3.85, 3.84, 3.82, 3.80, 3.79, 3.70, 3.60, 3.40, 3.20, 3.00};
  const int percents[] =   {100,  95,   90,   85,   80,   70,   60,   50,   40,   30,   20,   15,   10,   5,    4,    3,    2,    1,    0};
  const int points = 19;
  
  // Poniżej minimum
  if (v <= voltages[points - 1]) return 0;
  
  // Powyżej maximum
  if (v >= voltages[0]) return 100;
  
  // Interpolacja liniowa między punktami
  for (int i = 0; i < points - 1; i++) {
    if (v >= voltages[i + 1] && v <= voltages[i]) {
      float ratio = (v - voltages[i + 1]) / (voltages[i] - voltages[i + 1]);
      return percents[i + 1] + ratio * (percents[i] - percents[i + 1]);
    }
  }
  
  return 0;
}

// Sprawdzenie krytycznego poziomu baterii i shutdown
void checkBatteryAndShutdown(float voltage) {
  if (voltage < BATTERY_CRITICAL) {
    DPRINTF("KRYTYCZNY POZIOM BATERII: %.2fV\n", voltage);
    
    // Wyświetl komunikat ostrzegawczy
    displayCriticalBatteryWarning();
    
    // Wyłącz wszystko i przejdź w deep sleep
    DPRINTLN("Entering deep sleep due to low battery...");
    enterDeepSleep();
  }
}
