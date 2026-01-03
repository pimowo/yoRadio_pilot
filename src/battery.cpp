#include "battery.h"
#include "config.h"
#include "myoptions.h"

// Struktura przechowująca charakterystyki kalibracji ADC
static esp_adc_cal_characteristics_t adc_chars;

// Inicjalizacja pomiaru baterii
void batteryInit() {
  // Ustawienie rozdzielczości ADC na 12 bitów
  adc1_config_width(ADC_WIDTH_BIT_12);

  // Konfiguracja tłumienia napięcia dla konkretnego kanału ADC
  adc1_config_channel_atten(BATTERY_ADC_PIN, ADC_ATTEN_DB_12);

  // Kalibracja ADC z podaną wartością referencyjną (w mV)
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);
}

// Funkcja odczytu poziomu baterii w procentach (0-100%)
int readBatteryPercent() {
  uint32_t voltage = 0;
  
  // Średnia z 10 pomiarów ADC, aby wygładzić odczyty
  for (int i = 0; i < 10; i++) {
    voltage += esp_adc_cal_raw_to_voltage(adc1_get_raw(BATTERY_ADC_PIN), &adc_chars);
    delay(10); // krótkie opóźnienie między pomiarami
  }
  voltage /= 10; // obliczenie średniego napięcia
  
  // Uwzględnienie dzielnika napięcia na PCB
  voltage = (uint32_t)(voltage * BATTERY_DIVIDER_RATIO);
  
  // Mapowanie napięcia na procenty
  if (voltage >= BATTERY_MAX_MV) return 100; // pełna bateria
  if (voltage <= BATTERY_MIN_MV) return 0;   // bateria rozładowana
  
  // Obliczenie procentowego poziomu baterii
  int percent = map(voltage, BATTERY_MIN_MV, BATTERY_MAX_MV, 0, 100);
  return constrain(percent, 0, 100); // ograniczenie wartości do zakresu 0-100%
}