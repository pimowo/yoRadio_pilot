#ifndef CONFIG_H
#define CONFIG_H

//==================================================================================================
// FIRMWARE VERSION
#define FIRMWARE_VERSION "0.5.0"           // wersja oprogramowania

// ====================== USTAWIENIA / SETTINGS ======================
// Debug UART messages: ustaw na 1 aby włączyć diagnostykę po UART, 0 aby wyłączyć
#define DEBUG_UART 1

// Debug helpers
#if DEBUG_UART
  #define DPRINT(x) Serial.print(x)
  #define DPRINTLN(x) Serial.println(x)
  #define DPRINTF(fmt, ...) Serial.printf((fmt), __VA_ARGS__)
#else
  #define DPRINT(x)
  #define DPRINTLN(x)
  #define DPRINTF(fmt, ...)
#endif

// ===== NETWORK SETTINGS =====
#define WIFI_SSID   "pimowo"             // sieć 
#define WIFI_PASS   "ckH59LRZQzCDQFiUgj" // hasło sieci
#define STATIC_IP   "192.168.1.111"      // IP
#define GATEWAY_IP  "192.168.1.1"        // brama
#define SUBNET_MASK "255.255.255.0"      // maska
#define DNS1_IP     "192.168.1.1"        // DNS 1
#define DNS2_IP     "8.8.8.8"            // DNS 2

// ===== OTA SETTINGS =====
#define OTAhostname "yoRadio_pilot"      // nazwa dla OTA
#define OTApassword "12345987"           // hasło dla OTA

// ===== RADIO SETTINGS =====
// yoRadio - multi-radio support
const char* RADIO_IPS[] = {
  "192.168.1.101",                       // Radio 1
  "192.168.1.104",                       // Radio 2
  "192.168.1.133"                        // Radio 3
};
#define NUM_RADIOS 3                     // liczba dostępnych radiów

// ===== SLEEP SETTINGS =====
#define DEEP_SLEEP_TIMEOUT_SEC 60        // sekundy bezczynności przed deep sleep (podczas odtwarzania)
#define DEEP_SLEEP_TIMEOUT_STOPPED_SEC 5 // sekundy bezczynności przed deep sleep (gdy zatrzymany)

// ===== BUTTON PINS =====
#define BTN_UP     7                     // pin GÓRA
#define BTN_RIGHT  4                     // pin PRAWO
#define BTN_CENTER 5                     // pin OK
#define BTN_LEFT   6                     // pin LEWO 
#define BTN_DOWN   3                     // pin DÓŁ
#define LONG_PRESS_TIME 2000             // czas long-press w ms (2 sekundy)

// ===== DISPLAY SETTINGS =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_BRIGHTNESS 10               // 0-15 (wartość * 16 daje zakres 0-240 dla kontrastu SSD1306)
#define DISPLAY_REFRESH_RATE_MS 50       // odświeżanie ekranu (50ms = 20 FPS)
#define VOLUME_DISPLAY_TIME 2000         // czas wyświetlania głośności w ms

// ===== BATTERY SETTINGS =====
#define BATTERY_PIN 11                    // GPIO11 (ADC2_CH0)
#define BATTERY_SAMPLES 30                // Liczba próbek
#define BATTERY_CHECK_INTERVAL_MS 10000   // Co 10 sekund
#define BATTERY_CRITICAL 3.0              // Próg deep sleep
#define ADC_CORRECTION_FACTOR 1.0         // Kalibracja
#define BATTERY_LOW_BLINK_MS 500          // interwał mrugania słabej baterii

// ===== I2C PINS (ESP32-S3 Super Mini) =====
#define I2C_SDA 8
#define I2C_SCL 9

// ===== LED SETTINGS =====
#define LED_PIN 48       // GPIO 48
#define NUM_LEDS 1       // Ile LED-ów?  (1 jeśli jeden chipik)

// ===== WATCHDOG SETTINGS =====
#define WDT_TIMEOUT 30                   // timeout watchdog w sekundach

// ===== WEBSOCKET SETTINGS =====
#define WS_TIMEOUT_MS 10000              // 10 sekund timeout dla WebSocket
#define WS_RECONNECT_INTERVAL_MS 3000    // 3 sekundy interwał reconnect

#endif // CONFIG_H
