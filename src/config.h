#ifndef CONFIG_H
#define CONFIG_H

//==================================================================================================
// FIRMWARE
//==================================================================================================
#define FIRMWARE_VERSION "1.3"

//==================================================================================================
// TECHNICAL CONSTANTS
//==================================================================================================

// Display refresh
#define DISPLAY_REFRESH_RATE_MS 50

// Battery
#define BATTERY_ADC_PIN ADC1_CHANNEL_0
#define BATTERY_LOW_BLINK_MS 500

// Button pins (ESP32-C3 Super Mini)
#define BTN_UP     2
#define BTN_RIGHT  3
#define BTN_CENTER 4
#define BTN_LEFT   5
#define BTN_DOWN   6

// Volume control timing
#define VOLUME_REPEAT_DELAY_MS 250
#define VOLUME_REPEAT_RATE_MS 80

// Watchdog
#define WDT_TIMEOUT 30

//==================================================================================================
// LAYOUT CONSTANTS
//==================================================================================================

// Display layout
#define HEADER_HEIGHT 16
#define FOOTER_Y 52
#define FOOTER_HEIGHT 12

// Battery indicator
#define BAT_X 25
#define BAT_WIDTH 20
#define BAT_HEIGHT 8
#define BAT_TIP_WIDTH 2
#define BAT_TIP_HEIGHT 4

// Volume indicator  
#define VOL_X 54
#define VOL_ICON_SIZE 8
#define VOL_TEXT_OFFSET 10

// Bitrate/Format indicator
#define BITRATE_X 85

// Centered text positions
#define WIFI_ANIM_Y 15
#define WIFI_SSID_Y 35
#define VERSION_Y 52
#define YORADIO_TEXT_Y 20
#define YORADIO_DOTS_Y 32
#define YORADIO_IP_Y 52
#define ERROR_ICON_Y 10
#define ERROR_TEXT_Y 30

// Volume screen
#define VOL_SCREEN_HEADER_HEIGHT 16
#define VOL_SCREEN_NUMBER_Y 25
#define VOL_SCREEN_IP_Y 54

// Animation
#define WIFI_ANIM_INTERVAL_MS 250
#define YORADIO_ANIM_INTERVAL_MS 400
#define BLINK_INTERVAL_MS 1000

//==================================================================================================
// HARDWARE CONFIGURATION
//==================================================================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

//==================================================================================================
#endif // CONFIG_H