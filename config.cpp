#include "config.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

Config config;

// Default configuration values (from original #defines)
void resetConfig() {
  // WiFi defaults - CHANGE THESE FOR YOUR NETWORK
  // These are placeholder values - update via web interface after first boot
  strncpy(config.wifi_ssid, "YOUR_WIFI_SSID", sizeof(config.wifi_ssid) - 1);
  strncpy(config.wifi_pass, "YOUR_WIFI_PASSWORD", sizeof(config.wifi_pass) - 1);
  config.use_dhcp = true;  // Use DHCP by default for easier setup
  strncpy(config.static_ip, "192.168.1.111", sizeof(config.static_ip) - 1);
  strncpy(config.gateway, "192.168.1.1", sizeof(config.gateway) - 1);
  strncpy(config.subnet, "255.255.255.0", sizeof(config.subnet) - 1);
  strncpy(config.dns1, "192.168.1.1", sizeof(config.dns1) - 1);
  strncpy(config.dns2, "8.8.8.8", sizeof(config.dns2) - 1);
  
  // Radio defaults
  config.num_radios = 1;
  strncpy(config.radio_ips[0], "192.168.1.101", sizeof(config.radio_ips[0]) - 1);
  strncpy(config.radio_ips[1], "192.168.1.102", sizeof(config.radio_ips[1]) - 1);
  strncpy(config.radio_ips[2], "192.168.1.103", sizeof(config.radio_ips[2]) - 1);
  strncpy(config.radio_ips[3], "", sizeof(config.radio_ips[3]) - 1);
  strncpy(config.radio_ips[4], "", sizeof(config.radio_ips[4]) - 1);
  
  strncpy(config.radio_names[0], "Radio 1", sizeof(config.radio_names[0]) - 1);
  strncpy(config.radio_names[1], "Radio 2", sizeof(config.radio_names[1]) - 1);
  strncpy(config.radio_names[2], "Radio 3", sizeof(config.radio_names[2]) - 1);
  strncpy(config.radio_names[3], "Radio 4", sizeof(config.radio_names[3]) - 1);
  strncpy(config.radio_names[4], "Radio 5", sizeof(config.radio_names[4]) - 1);
  
  config.default_radio = 0;
  
  // Timeout defaults
  config.deep_sleep_timeout = 60;
  config.deep_sleep_timeout_stopped = 5;
  config.long_press_time = 2000;
  
  // OTA defaults
  strncpy(config.ota_hostname, "yoRadio_pilot", sizeof(config.ota_hostname) - 1);
  strncpy(config.ota_password, "12345987", sizeof(config.ota_password) - 1);
  
  // Display defaults
  config.oled_brightness = 10;
  config.display_refresh_rate = 50;
  
  // Debug default
  config.debug_uart = false;
}

bool configExists() {
  return SPIFFS.exists("/config.json");
}

void loadConfig() {
  // Initialize with defaults first
  resetConfig();
  
  // Try to load from SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
    return;
  }
  
  if (!configExists()) {
    Serial.println("Config file not found, using defaults");
    return;
  }
  
  File file = SPIFFS.open("/config.json", "r");
  if (!file) {
    Serial.println("Failed to open config file");
    return;
  }
  
  // Parse JSON
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    Serial.print("Failed to parse config: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Load WiFi settings
  if (doc.containsKey("wifi_ssid")) 
    strncpy(config.wifi_ssid, doc["wifi_ssid"], sizeof(config.wifi_ssid) - 1);
  if (doc.containsKey("wifi_pass")) 
    strncpy(config.wifi_pass, doc["wifi_pass"], sizeof(config.wifi_pass) - 1);
  if (doc.containsKey("use_dhcp")) 
    config.use_dhcp = doc["use_dhcp"];
  if (doc.containsKey("static_ip")) 
    strncpy(config.static_ip, doc["static_ip"], sizeof(config.static_ip) - 1);
  if (doc.containsKey("gateway")) 
    strncpy(config.gateway, doc["gateway"], sizeof(config.gateway) - 1);
  if (doc.containsKey("subnet")) 
    strncpy(config.subnet, doc["subnet"], sizeof(config.subnet) - 1);
  if (doc.containsKey("dns1")) 
    strncpy(config.dns1, doc["dns1"], sizeof(config.dns1) - 1);
  if (doc.containsKey("dns2")) 
    strncpy(config.dns2, doc["dns2"], sizeof(config.dns2) - 1);
  
  // Load radio settings
  if (doc.containsKey("num_radios")) 
    config.num_radios = doc["num_radios"];
  if (doc.containsKey("default_radio")) 
    config.default_radio = doc["default_radio"];
  
  if (doc.containsKey("radio_ips")) {
    JsonArray ips = doc["radio_ips"].as<JsonArray>();
    int i = 0;
    for (JsonVariant ip : ips) {
      if (i < 5) {
        strncpy(config.radio_ips[i], ip.as<const char*>(), sizeof(config.radio_ips[i]) - 1);
        i++;
      }
    }
  }
  
  if (doc.containsKey("radio_names")) {
    JsonArray names = doc["radio_names"].as<JsonArray>();
    int i = 0;
    for (JsonVariant name : names) {
      if (i < 5) {
        strncpy(config.radio_names[i], name.as<const char*>(), sizeof(config.radio_names[i]) - 1);
        i++;
      }
    }
  }
  
  // Load timeout settings
  if (doc.containsKey("deep_sleep_timeout")) 
    config.deep_sleep_timeout = doc["deep_sleep_timeout"];
  if (doc.containsKey("deep_sleep_timeout_stopped")) 
    config.deep_sleep_timeout_stopped = doc["deep_sleep_timeout_stopped"];
  if (doc.containsKey("long_press_time")) 
    config.long_press_time = doc["long_press_time"];
  
  // Load OTA settings
  if (doc.containsKey("ota_hostname")) 
    strncpy(config.ota_hostname, doc["ota_hostname"], sizeof(config.ota_hostname) - 1);
  if (doc.containsKey("ota_password")) 
    strncpy(config.ota_password, doc["ota_password"], sizeof(config.ota_password) - 1);
  
  // Load display settings
  if (doc.containsKey("oled_brightness")) 
    config.oled_brightness = doc["oled_brightness"];
  if (doc.containsKey("display_refresh_rate")) 
    config.display_refresh_rate = doc["display_refresh_rate"];
  
  // Load debug setting
  if (doc.containsKey("debug_uart")) 
    config.debug_uart = doc["debug_uart"];
  
  Serial.println("Configuration loaded successfully");
}

void saveConfig() {
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
    return;
  }
  
  // Create JSON document
  StaticJsonDocument<2048> doc;
  
  // WiFi settings
  doc["wifi_ssid"] = config.wifi_ssid;
  doc["wifi_pass"] = config.wifi_pass;
  doc["use_dhcp"] = config.use_dhcp;
  doc["static_ip"] = config.static_ip;
  doc["gateway"] = config.gateway;
  doc["subnet"] = config.subnet;
  doc["dns1"] = config.dns1;
  doc["dns2"] = config.dns2;
  
  // Radio settings
  doc["num_radios"] = config.num_radios;
  doc["default_radio"] = config.default_radio;
  
  JsonArray ips = doc.createNestedArray("radio_ips");
  for (int i = 0; i < 5; i++) {
    ips.add(config.radio_ips[i]);
  }
  
  JsonArray names = doc.createNestedArray("radio_names");
  for (int i = 0; i < 5; i++) {
    names.add(config.radio_names[i]);
  }
  
  // Timeout settings
  doc["deep_sleep_timeout"] = config.deep_sleep_timeout;
  doc["deep_sleep_timeout_stopped"] = config.deep_sleep_timeout_stopped;
  doc["long_press_time"] = config.long_press_time;
  
  // OTA settings
  doc["ota_hostname"] = config.ota_hostname;
  doc["ota_password"] = config.ota_password;
  
  // Display settings
  doc["oled_brightness"] = config.oled_brightness;
  doc["display_refresh_rate"] = config.display_refresh_rate;
  
  // Debug setting
  doc["debug_uart"] = config.debug_uart;
  
  // Write to file
  File file = SPIFFS.open("/config.json", "w");
  if (!file) {
    Serial.println("Failed to create config file");
    return;
  }
  
  if (serializeJson(doc, file) == 0) {
    Serial.println("Failed to write config");
  } else {
    Serial.println("Configuration saved successfully");
  }
  
  file.close();
}
