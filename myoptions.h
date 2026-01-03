#ifndef MYOPTIONS_H
#define MYOPTIONS_H

// === DEBUG ======================================================================================
#define DEBUG_UART 0 // ustaw na 1 aby włączyć diagnostykę po UART, 0 aby wyłączyć

// === SIEĆ =======================================================================================
#define WIFI_SSID "pimowo"               // sieć 
#define WIFI_PASS "ckH59LRZQzCDQFiUgj"   // hasło sieci
#define STATIC_IP "192.168.1.123"        // IP
#define GATEWAY_IP "192.168.1.1"         // brama
#define SUBNET_MASK "255.255.255.0"      // maska
#define DNS1_IP "192.168.1.1"            // DNS 1
#define DNS2_IP "8.8.8.8"                // DNS 2
#define USE_STATIC_IP 1                  // 1 = statyczne IP, 0 = DHCP

// === YORADIO ====================================================================================
#define YORADIO_IP "192.168.1.122"       // ZMIEŃ na IP twojego yoRadio

// === WYŚWIETLACZ ================================================================================
#define OLED_BRIGHTNESS 10               // 0-15 (wartość * 16 daje zakres 0-240 dla kontrastu SSD1306)

// === BATERIA ====================================================================================
#define BATTERY_MIN_MV 3000              // 3.0V = pusta bateria
#define BATTERY_MAX_MV 4200              // 4.2V = pełna bateria
#define BATTERY_DIVIDER_RATIO 2.0        // Jeśli używasz dzielnika napięcia (R1=R2)

// === DEEP SLEEP =================================================================================
#define DEEP_SLEEP_TIMEOUT_SEC 30        // sekundy bezczynności przed deep sleep (podczas odtwarzania)
#define DEEP_SLEEP_TIMEOUT_STOPPED_SEC 15 // sekundy bezczynności przed deep sleep (gdy zatrzymany)

//=================================================================================================
#endif // MYOPTIONS_H