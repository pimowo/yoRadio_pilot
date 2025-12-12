#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Configuration structure
struct Config {
  // WiFi
  char wifi_ssid[32];
  char wifi_pass[64];
  bool use_dhcp;
  char static_ip[16];
  char gateway[16];
  char subnet[16];
  char dns1[16];
  char dns2[16];
  
  // Radia (yoRadio)
  int num_radios;
  char radio_ips[5][16];
  char radio_names[5][32];
  int default_radio;
  
  // Timeouty
  int deep_sleep_timeout;
  int deep_sleep_timeout_stopped;
  int long_press_time;
  
  // OTA
  char ota_hostname[32];
  char ota_password[32];
  
  // Display
  int oled_brightness;
  int display_refresh_rate;
  
  // Debug
  bool debug_uart;
};

extern Config config;

// Function declarations
void loadConfig();
void saveConfig();
void resetConfig();
bool configExists();

#endif // CONFIG_H
