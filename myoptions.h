#ifndef MYOPTIONS_H
#define MYOPTIONS_H

// === DEBUG ======================================================================================
#define DEBUG_UART 0 // 1 = włącz diagnostykę przez UART, 0 = wyłącz

// === SIEĆ =======================================================================================
#define WIFI_SSID "pimowo"               // nazwa sieci Wi-Fi
#define WIFI_PASS "ckH59LRZQzCDQFiUgj"   // hasło do sieci Wi-Fi
#define STATIC_IP "192.168.1.123"        // statyczny adres IP urządzenia
#define GATEWAY_IP "192.168.1.1"         // adres bramy sieciowej
#define SUBNET_MASK "255.255.255.0"      // maska podsieci
#define DNS1_IP "192.168.1.1"            // pierwszy serwer DNS
#define DNS2_IP "8.8.8.8"                // drugi serwer DNS
#define USE_STATIC_IP 1                  // 1 = używaj statycznego IP, 0 = DHCP

// === YORADIO ====================================================================================
#define YORADIO_IP "192.168.1.122"       // IP twojego yoRadio (zmień na swoje)

// === WYŚWIETLACZ ================================================================================ 
#define OLED_BRIGHTNESS 10               // jasność OLED 0-15 (wartość *16 = 0-240 kontrastu SSD1306)

// === BATERIA ====================================================================================
#define BATTERY_MIN_MV 3000              // napięcie minimalne (pusta bateria) w mV
#define BATTERY_MAX_MV 4200              // napięcie maksymalne (pełna bateria) w mV
#define BATTERY_DIVIDER_RATIO 2.0        // współczynnik dzielnika napięcia (jeśli używasz R1=R2)

// === DEEP SLEEP =================================================================================
#define DEEP_SLEEP_TIMEOUT_SEC 30        // czas bezczynności w sekundach przed deep sleep podczas odtwarzania
#define DEEP_SLEEP_TIMEOUT_STOPPED_SEC 15 // czas bezczynności w sekundach przed deep sleep, gdy urządzenie zatrzymane

//=================================================================================================
#endif // MYOPTIONS_H
