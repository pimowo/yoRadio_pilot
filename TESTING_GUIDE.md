# Testing Guide for yoRadio Pilot Web Configuration

## Overview
This guide helps verify that the web configuration interface works correctly.

## Pre-Upload Checklist

1. **Update Default WiFi Credentials** (Optional)
   - Edit `config.cpp` lines 11-12
   - Replace `YOUR_WIFI_SSID` and `YOUR_WIFI_PASSWORD` with your network
   - This allows the device to connect automatically on first boot

2. **Verify Dependencies in platformio.ini**
   ```ini
   lib_deps = 
       adafruit/Adafruit SSD1306@^2.5.7
       adafruit/Adafruit NeoPixel@^1.10.7
       adafruit/Adafruit BusIO@^1.14.1
       links2004/WebSockets@^2.3.6
       bblanchon/ArduinoJson@^6.21.2
       me-no-dev/ESPAsyncWebServer@^1.2.3
       me-no-dev/AsyncTCP@^1.1.1
   ```

3. **Check Partition Scheme**
   - Ensure ESP32 has SPIFFS support
   - Recommended: `default.csv` partition scheme
   - Minimum 1MB SPIFFS for configuration storage

## Upload Process

### Using PlatformIO
```bash
# Build and upload firmware
pio run --target upload

# Monitor serial output
pio device monitor
```

### Using Arduino IDE
1. Install all required libraries
2. Select ESP32 board
3. Select partition scheme with SPIFFS
4. Upload sketch
5. Open Serial Monitor (115200 baud)

## Testing Scenarios

### Test 1: First Boot (No Configuration)

**Expected Behavior:**
1. Device starts with default configuration
2. Attempts to connect to WiFi (will fail with placeholder credentials)
3. After 15 seconds, creates Access Point
4. Serial monitor shows:
   ```
   Starting YoRadio OLED Display v0.3
   Configuration loaded successfully (or "using defaults")
   Connecting to WiFi: YOUR_WIFI_SSID
   WiFi connection timeout!
   Starting Access Point mode...
   AP Password: yoRadioXXXXXXXX
   ```

**Test Steps:**
1. Note the AP password from serial monitor
2. Connect to `yoRadio_pilot_setup` WiFi
3. Open browser to `http://192.168.4.1`
4. Verify web interface loads
5. Configure WiFi settings
6. Click "Save Configuration"
7. Click "Restart"

**Pass Criteria:**
- ✅ AP mode activates after timeout
- ✅ Web interface accessible at 192.168.4.1
- ✅ Configuration form displays all sections
- ✅ Can save configuration
- ✅ Device restarts successfully

### Test 2: WiFi Connection

**Expected Behavior:**
1. Device loads configuration from SPIFFS
2. Connects to configured WiFi
3. Obtains IP address (DHCP or static)
4. Web server starts on port 80
5. Connects to yoRadio WebSocket

**Test Steps:**
1. After restart, monitor serial output
2. Note the IP address assigned
3. On your computer/phone, open browser to device IP
4. Verify web interface loads

**Pass Criteria:**
- ✅ WiFi connects successfully
- ✅ IP address displayed in serial monitor
- ✅ Web interface accessible at device IP
- ✅ Status section shows correct information
- ✅ WebSocket connects to radio

### Test 3: Configuration Persistence

**Expected Behavior:**
Configuration survives restarts and deep sleep cycles.

**Test Steps:**
1. Access web interface
2. Change multiple settings:
   - WiFi SSID/password
   - Radio IPs and names
   - Deep sleep timeouts
   - OLED brightness
   - Debug UART enable
3. Save configuration
4. Restart device
5. Access web interface again
6. Verify all settings preserved

**Pass Criteria:**
- ✅ All changed settings persist
- ✅ SPIFFS contains `/config.json`
- ✅ Settings apply after restart

### Test 4: Radio Switching

**Expected Behavior:**
Multiple radios can be configured and switched.

**Test Steps:**
1. Configure 2+ radios with different IPs
2. Save and restart
3. Verify WebSocket connects to first radio
4. Short press CENTER button → toggle play/pause
5. Long press CENTER button (2 seconds) → switch to next radio
6. Observe display updates to new radio info
7. Enter deep sleep
8. Wake device
9. Verify still on same radio (RTC persistence)

**Pass Criteria:**
- ✅ Can configure multiple radios
- ✅ Short press toggles play/pause
- ✅ Long press switches radios
- ✅ Display shows current radio number
- ✅ Radio selection persists through deep sleep

### Test 5: Display Settings

**Expected Behavior:**
OLED brightness and refresh rate are configurable.

**Test Steps:**
1. Set OLED brightness to 5
2. Set refresh rate to 100ms
3. Save and restart
4. Observe dimmer display
5. Note slower scroll animation
6. Change brightness to 15
7. Save and restart
8. Observe brighter display

**Pass Criteria:**
- ✅ Brightness changes take effect
- ✅ Refresh rate changes affect animation speed
- ✅ Settings persist after restart

### Test 6: Debug UART

**Expected Behavior:**
Debug messages can be enabled/disabled.

**Test Steps:**
1. Disable DEBUG UART in web interface
2. Save and restart
3. Observe serial monitor (minimal output)
4. Enable DEBUG UART
5. Save and restart
6. Observe verbose serial output

**Pass Criteria:**
- ✅ Debug messages appear when enabled
- ✅ Debug messages suppressed when disabled
- ✅ Critical messages always appear

### Test 7: OTA Update

**Expected Behavior:**
OTA hostname and password work correctly.

**Test Steps:**
1. Configure OTA settings in web interface
2. Save configuration
3. Click "Update Firmware" button
4. Should redirect to `/update`
5. Verify OTA update page accessible

**Pass Criteria:**
- ✅ OTA update page loads
- ✅ Can upload new firmware
- ✅ Password protection works

### Test 8: Deep Sleep

**Expected Behavior:**
Deep sleep timeouts work as configured.

**Test Steps:**
1. Set "deep sleep when stopped" to 10 seconds
2. Set "deep sleep when playing" to 30 seconds
3. Save and restart
4. Stop playback (if playing)
5. Don't touch buttons
6. After 10 seconds, device should sleep
7. Press CENTER to wake
8. Start playback
9. Don't touch buttons
10. After 30 seconds, device should sleep

**Pass Criteria:**
- ✅ Sleeps after configured timeout when stopped
- ✅ Sleeps after configured timeout when playing
- ✅ Wakes on button press
- ✅ Reconnects to WiFi and WebSocket

### Test 9: Static IP Configuration

**Expected Behavior:**
Can switch between DHCP and static IP.

**Test Steps:**
1. Access web interface
2. Uncheck "Use DHCP"
3. Configure static IP settings
4. Save and restart
5. Verify device uses static IP
6. Re-enable DHCP
7. Save and restart
8. Verify device obtains IP via DHCP

**Pass Criteria:**
- ✅ Static IP works correctly
- ✅ DHCP works correctly
- ✅ Can switch between modes

### Test 10: API Endpoints

**Expected Behavior:**
REST API endpoints work correctly.

**Test Steps:**
1. GET `http://device-ip/api/status`
   - Should return JSON with version, RSSI, IP, heap, uptime
2. GET `http://device-ip/api/config`
   - Should return full configuration as JSON
3. POST to `/api/config` with JSON body
   - Should save configuration
4. POST to `/api/restart`
   - Should restart device
5. POST to `/api/reset`
   - Should reset to defaults

**Pass Criteria:**
- ✅ All endpoints return expected data
- ✅ Status updates reflect real-time values
- ✅ Config changes via API work
- ✅ Restart/reset functions work

### Test 11: Button Functionality

**Expected Behavior:**
All buttons work as before.

**Test Steps:**
1. UP button → volume increase
2. DOWN button → volume decrease
3. CENTER short press → toggle play/pause
4. CENTER long press → switch radio (if >1 configured)
5. LEFT button → previous station
6. RIGHT button → next station

**Pass Criteria:**
- ✅ All buttons respond correctly
- ✅ Volume display shows for 2 seconds
- ✅ Long press timing configurable
- ✅ Commands sent to radio via WebSocket

### Test 12: Display Functionality

**Expected Behavior:**
OLED display works correctly with scrolling text.

**Test Steps:**
1. Observe station name scrolling
2. Observe artist name scrolling
3. Observe track name scrolling
4. Verify Polish characters display correctly
5. Check battery indicator
6. Check volume indicator
7. Check radio number (if >1 configured)
8. Check bitrate/format display

**Pass Criteria:**
- ✅ All text displays correctly
- ✅ Scrolling works smoothly
- ✅ Polish characters render properly
- ✅ All indicators visible and accurate

## Common Issues and Solutions

### Issue: Device doesn't create AP
**Solution:** Wait full 15 seconds. Check serial monitor for errors.

### Issue: Can't access web interface
**Solution:** Verify IP address. Check that device is on same network. Try 192.168.4.1 in AP mode.

### Issue: Configuration not saving
**Solution:** Check SPIFFS is mounted. Verify partition scheme includes SPIFFS. Check serial monitor for errors.

### Issue: WiFi not connecting
**Solution:** Verify SSID and password correct. Check signal strength. Use AP mode to reconfigure.

### Issue: WebSocket not connecting
**Solution:** Verify radio IP is correct and reachable. Check that yoRadio is running on target device.

### Issue: Display not updating
**Solution:** Check refresh rate setting. Verify WebSocket connected. Check debug output.

### Issue: Buttons not working
**Solution:** Verify button pin definitions. Check physical connections. Enable debug to see button events.

## Serial Monitor Expected Output

```
Starting YoRadio OLED Display v0.3
Configuration loaded successfully
Wakeup reason: UNDEFINED / normal boot
Current radio: 1 (192.168.1.101)
Watchdog timer initialized
OLED brightness set to: 10
Connecting to WiFi: MyNetwork
Using static IP: 192.168.1.111
WiFi connected!
IP: 192.168.1.111
Web server started on port 80
Connecting to WebSocket at 192.168.1.101:80/ws
OTA ready
WebSocket connected!
```

## Performance Metrics

Expected values:
- **Boot time:** 3-5 seconds
- **WiFi connection:** 2-3 seconds
- **WebSocket connection:** 1-2 seconds
- **Web page load:** <1 second
- **Config save time:** <500ms
- **Free heap after boot:** >200KB
- **Display refresh rate:** Configurable 20-200ms

## Security Checklist

- ✅ AP password is unique per device (MAC-based)
- ✅ No hardcoded WiFi credentials in public code
- ✅ All string operations use safe_strncpy
- ✅ Buffer overflow protection on all inputs
- ✅ OTA password protection enabled
- ⚠️ Web interface has no authentication (consider adding for production)
- ⚠️ WiFi credentials stored unencrypted in SPIFFS

## Final Verification

Before considering testing complete, verify:
- [ ] All 12 test scenarios pass
- [ ] No memory leaks (heap stable over time)
- [ ] No crashes or unexpected reboots
- [ ] Configuration survives multiple power cycles
- [ ] Deep sleep works reliably
- [ ] All original features work correctly
- [ ] Web interface works on desktop and mobile
- [ ] Serial output clean (no errors)
- [ ] OTA updates work

## Reporting Issues

When reporting issues, include:
1. ESP32 board model
2. Firmware version
3. Serial monitor output
4. Configuration JSON (if relevant)
5. Steps to reproduce
6. Expected vs actual behavior

## Next Steps

After successful testing:
1. Consider adding HTTP authentication to web interface
2. Consider encrypting WiFi credentials in SPIFFS
3. Add WiFi network scanning feature
4. Add radio ping/availability test
5. Add configuration export/import
6. Monitor long-term stability
