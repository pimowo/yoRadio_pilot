# yoRadio Pilot - Web Configuration Interface

## Overview

This implementation adds a web-based configuration interface to yoRadio_pilot, eliminating the need to recompile the firmware for configuration changes.

## Features

### Web Server
- **ESPAsyncWebServer** for better performance
- Runs on port 80
- Concurrent operation with WebSocket and OTA
- Responsive dark-themed interface

### Configuration Sections

#### 1. WiFi Configuration
- SSID and password
- DHCP or Static IP option
- Static IP, Gateway, Subnet mask
- DNS1 and DNS2 servers

#### 2. Radio Configuration (yoRadio)
- Support for up to 5 radios
- IP address for each radio
- Optional name for each radio
- Default radio selection

#### 3. Timeouts
- Deep sleep timeout during playback (seconds)
- Deep sleep timeout when stopped (seconds)
- Long-press duration (milliseconds)

#### 4. OTA
- OTA hostname
- OTA password

#### 5. Display
- OLED brightness (0-15)
- Display refresh rate (milliseconds)

#### 6. Debug
- Enable/disable DEBUG UART logging

### SPIFFS Storage
- Configuration saved to `/config.json`
- Loaded automatically on boot
- Default values used when config file doesn't exist

### Access Point Mode
- Activates after 15 seconds if WiFi connection fails
- SSID: `yoRadio_pilot_setup`
- Password: Generated from ESP32 MAC address (check Serial Monitor)
  - Format: `yoRadio` + chip ID in hex (e.g., `yoRadio1a2b3c4d`)
  - More secure than fixed password
- IP: `192.168.4.1`
- Access web interface at `http://192.168.4.1`

### Web Interface Actions

#### Buttons
- **💾 Save Configuration** - Saves to SPIFFS (restart required to apply)
- **🔄 Restart** - Restarts the ESP32
- **⚠️ Reset to Defaults** - Deletes config.json and restarts
- **⬆️ Update Firmware** - Redirects to OTA update page

#### Status Display
Updates every 2 seconds:
- Firmware version
- WiFi RSSI (signal strength)
- Current IP address
- Free heap memory
- Uptime

## API Endpoints

### GET /
Main configuration page (HTML)

### GET /api/config
Returns current configuration as JSON

Example response:
```json
{
  "wifi_ssid": "MyNetwork",
  "wifi_pass": "password",
  "use_dhcp": false,
  "static_ip": "192.168.1.111",
  "gateway": "192.168.1.1",
  "subnet": "255.255.255.0",
  "dns1": "192.168.1.1",
  "dns2": "8.8.8.8",
  "num_radios": 1,
  "default_radio": 0,
  "radio_ips": ["192.168.1.101", "192.168.1.102", "192.168.1.103", "", ""],
  "radio_names": ["Radio 1", "Radio 2", "Radio 3", "Radio 4", "Radio 5"],
  "deep_sleep_timeout": 60,
  "deep_sleep_timeout_stopped": 5,
  "long_press_time": 2000,
  "ota_hostname": "yoRadio_pilot",
  "ota_password": "12345987",
  "oled_brightness": 10,
  "display_refresh_rate": 50,
  "debug_uart": false
}
```

### POST /api/config
Saves configuration (JSON body with same structure as GET response)

### GET /api/status
Returns device status as JSON

Example response:
```json
{
  "version": "0.3",
  "rssi": -45,
  "ip": "192.168.1.111",
  "heap": 234567,
  "uptime": "1h 23m 45s"
}
```

### POST /api/restart
Restarts the ESP32

### POST /api/reset
Resets configuration to defaults and restarts

## File Structure

```
yoRadio_pilot/
├── yoRadio_pilot.ino    # Main sketch (modified)
├── config.h             # Configuration structure
├── config.cpp           # Configuration management (SPIFFS)
├── webserver.h          # Web server declarations
├── webserver.cpp        # Web server implementation
├── font5x7.h            # 5x7 bitmap font
└── platformio.ini       # PlatformIO configuration
```

## Installation

### Using PlatformIO

1. Open the project in PlatformIO
2. Build and upload:
   ```bash
   pio run --target upload
   ```
3. Upload SPIFFS filesystem (if needed):
   ```bash
   pio run --target uploadfs
   ```

### Using Arduino IDE

1. Install required libraries:
   - Adafruit SSD1306 (^2.5.7)
   - Adafruit NeoPixel (^1.10.7)
   - Adafruit BusIO (^1.14.1)
   - WebSockets by Links2004 (^2.3.6)
   - ArduinoJson (^6.21.2)
   - ESPAsyncWebServer
   - AsyncTCP

2. Ensure ESP32 board support is installed
3. Select appropriate board and port
4. Upload the sketch

## First-Time Setup

1. Upload the firmware
2. Device will create an Access Point if no configuration exists
3. Check Serial Monitor for AP password (format: `yoRadio` + chip ID)
4. Connect to `yoRadio_pilot_setup` with the displayed password
5. Open browser to `http://192.168.4.1`
6. Configure WiFi settings
7. Save and restart
8. Device will connect to your WiFi network
9. Access configuration at device's IP address

## Configuration

Default configuration uses placeholder values:
- WiFi SSID: `YOUR_WIFI_SSID` (must be configured)
- WiFi Password: `YOUR_WIFI_PASSWORD` (must be configured)
- DHCP: Enabled by default
- Radio IP: `192.168.1.101`
- All other settings match the original firmware defaults

**Important:** Update WiFi credentials through the web interface on first boot, or the device will start in AP mode.

## Partition Scheme

Ensure your ESP32 has a partition scheme that includes SPIFFS:
- Minimum 1MB SPIFFS recommended
- Default partition scheme in platformio.ini uses `default.csv`

## Security Considerations

- AP password is generated from chip MAC address for uniqueness
- Web interface has no authentication by default
- Consider adding HTTP authentication for production use
- OTA password should be changed from default
- WiFi credentials are stored in SPIFFS (not encrypted)

## Troubleshooting

### Can't access web interface
- Check device is connected to network
- Find IP address from serial monitor or router
- Try accessing via AP mode

### Configuration not saving
- Check SPIFFS is properly initialized
- Verify partition scheme has space for SPIFFS
- Check serial monitor for error messages

### WiFi not connecting
- Verify SSID and password are correct
- Check signal strength
- Wait 15 seconds for AP mode to activate
- Use AP mode to reconfigure WiFi settings

### Changes not taking effect
- Configuration changes require a restart
- Click "Restart" button after saving
- Wait for device to reboot (about 10 seconds)

## Future Enhancements (Nice-to-Have)

- [ ] WiFi network scanning
- [ ] Radio ping test
- [ ] Configuration export/import (JSON download/upload)
- [ ] HTTP Basic Authentication
- [ ] WebSocket status in real-time
- [ ] Currently playing radio/station display

## Technical Notes

- All configuration is stored in `/config.json` on SPIFFS
- RTC memory preserves current radio selection through deep sleep
- Web server runs asynchronously, doesn't block main loop
- Original functionality (buttons, OLED, WebSocket) preserved
- Debug macros now controlled by `config.debug_uart` flag
