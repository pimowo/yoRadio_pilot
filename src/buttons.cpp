#include "battery.h"
#include "config.h"
#include "myoptions.h"

// ADC calibration characteristics
static esp_adc_cal_characteristics_t adc_chars;

void batteryInit() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(BATTERY_ADC_PIN, ADC_ATTEN_DB_12);
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);
}

int readBatteryPercent() {
  uint32_t voltage = 0;
  
  // Average 10 readings
  for (int i = 0; i < 10; i++) {
    voltage += esp_adc_cal_raw_to_voltage(adc1_get_raw(BATTERY_ADC_PIN), &adc_chars);
    delay(10);
  }
  voltage /= 10;
  
  // Apply voltage divider ratio
  voltage = (uint32_t)(voltage * BATTERY_DIVIDER_RATIO);
  
  // Map to percentage
  if (voltage >= BATTERY_MAX_MV) return 100;
  if (voltage <= BATTERY_MIN_MV) return 0;
  
  int percent = map(voltage, BATTERY_MIN_MV, BATTERY_MAX_MV, 0, 100);
  return constrain(percent, 0, 100);
}