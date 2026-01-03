#ifndef CONFIG_H
#define CONFIG_H

//==================================================================================================
// WERSJA OPROGRAMOWANIA
//==================================================================================================
#define FIRMWARE_VERSION "1.3"  // Wersja firmware

//==================================================================================================
// STAŁE TECHNICZNE
//==================================================================================================

// Odświeżanie wyświetlacza (ms)
#define DISPLAY_REFRESH_RATE_MS 50

// Pomiar baterii
#define BATTERY_ADC_PIN ADC1_CHANNEL_0  // Kanał ADC podłączony do dzielnika napięcia baterii
#define BATTERY_LOW_BLINK_MS 500        // Czas migania ikony baterii przy niskim poziomie (ms)

// Przyciski (ESP32-C3 Super Mini)
#define BTN_UP     2
#define BTN_RIGHT  3
#define BTN_CENTER 4
#define BTN_LEFT   5
#define BTN_DOWN   6

// Sterowanie głośnością
#define VOLUME_REPEAT_DELAY_MS 250  // Opóźnienie przed powtarzaniem przytrzymania przycisku (ms)
#define VOLUME_REPEAT_RATE_MS 80    // Częstotliwość powtarzania zmiany głośności przy przytrzymaniu (ms)

// Watchdog
#define WDT_TIMEOUT 30  // Czas timeout watchdog w sekundach

//==================================================================================================
// STAŁE UKŁADU WYŚWIETLACZA
//==================================================================================================

// Układ wyświetlacza
#define HEADER_HEIGHT 16
#define FOOTER_Y 52
#define FOOTER_HEIGHT 12

// Wskaźnik baterii
#define BAT_X 25
#define BAT_WIDTH 20
#define BAT_HEIGHT 8
#define BAT_TIP_WIDTH 2
#define BAT_TIP_HEIGHT 4

// Wskaźnik głośności  
#define VOL_X 54
#define VOL_ICON_SIZE 8
#define VOL_TEXT_OFFSET 10

// Wskaźnik bitrate/formatu
#define BITRATE_X 85

// Pozycje tekstu wyśrodkowanego
#define WIFI_ANIM_Y 15
#define WIFI_SSID_Y 35
#define VERSION_Y 52
#define YORADIO_TEXT_Y 20
#define YORADIO_DOTS_Y 32
#define YORADIO_IP_Y 52
#define ERROR_ICON_Y 10
#define ERROR_TEXT_Y 30

// Ekran głośności
#define VOL_SCREEN_HEADER_HEIGHT 16
#define VOL_SCREEN_NUMBER_Y 25
#define VOL_SCREEN_IP_Y 54

// Animacje
#define WIFI_ANIM_INTERVAL_MS 250
#define YORADIO_ANIM_INTERVAL_MS 400
#define BLINK_INTERVAL_MS 1000

//==================================================================================================
// KONFIGURACJA SPRZĘTU
//==================================================================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Brak resetu sprzętowego

//==================================================================================================
#endif // CONFIG_H