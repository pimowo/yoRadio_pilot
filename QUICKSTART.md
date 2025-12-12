# Quick Start Guide - yoRadio Pilot Web Configuration

## 🚀 Get Started in 5 Minutes

### Step 1: Upload Firmware

**Using PlatformIO:**
```bash
cd yoRadio_pilot
pio run --target upload
```

**Using Arduino IDE:**
1. Open `yoRadio_pilot.ino`
2. Install required libraries (see below)
3. Select ESP32 board
4. Select partition scheme with SPIFFS
5. Upload

### Step 2: First Boot

1. Open Serial Monitor (115200 baud)
2. Device will try to connect to WiFi (will fail with default credentials)
3. After 15 seconds, Access Point mode activates
4. Look for this in serial output:
   ```
   Starting Access Point mode...
   AP Password: yoRadioXXXXXXXX
   ```
5. **Note the password!**

### Step 3: Connect to Device

1. On your phone/computer, connect to WiFi:
   - Network: `yoRadio_pilot_setup`
   - Password: (from serial monitor, e.g., `yoRadio1a2b3c4d`)
2. Open browser to: `http://192.168.4.1`

### Step 4: Configure WiFi

1. Web interface loads automatically
2. In **WiFi section**:
   - Enter your WiFi SSID
   - Enter your WiFi password
   - Choose DHCP or Static IP
3. Click **💾 Save Configuration**
4. Click **🔄 Restart**

### Step 5: Access on Your Network

1. Device restarts and connects to your WiFi
2. Check Serial Monitor for IP address:
   ```
   WiFi connected!
   IP: 192.168.1.xxx
   ```
3. Open browser to that IP address
4. Configure remaining settings!

---

## 📋 Required Libraries

Install these in Arduino IDE (Library Manager) or PlatformIO (automatic):

- Adafruit SSD1306
- Adafruit NeoPixel
- Adafruit BusIO
- WebSockets (by Links2004)
- ArduinoJson
- ESPAsyncWebServer
- AsyncTCP

---

## ⚙️ Quick Configuration Tips

### WiFi Settings
- **Use DHCP** for easiest setup (checked by default)
- Static IP only if you need fixed address

### Radio Settings  
- Enter IP address of your yoRadio device
- Example: `192.168.1.100`
- Give it a friendly name like "Kitchen Radio"
- For multiple radios, increase "Number of radios"

### Display Settings
- **Brightness**: 10 is good default (0=dim, 15=bright)
- **Refresh rate**: 50ms is smooth (lower=faster)

### Timeouts
- **Deep sleep (playing)**: 60s = 1 minute
- **Deep sleep (stopped)**: 5s = quick sleep when idle
- **Long press**: 2000ms = 2 seconds hold

### Debug
- Enable **DEBUG UART** to see detailed logs
- Disable for production to reduce noise

---

## 🎮 Using the Device

### Buttons
- **UP** - Volume up
- **DOWN** - Volume down  
- **CENTER** (short) - Play/Pause
- **CENTER** (long, 2s) - Switch radio (if multiple configured)
- **LEFT** - Previous station
- **RIGHT** - Next station

### Display
- **Top line** - Station name (scrolls if long)
- **Middle line** - Artist name (scrolls if long)
- **Bottom line** - Track name (scrolls if long)
- **Bottom bar** - Radio number, battery, volume, bitrate

---

## 🔧 Common Issues

### "Can't connect to AP"
- Check password carefully (case-sensitive)
- Look in serial monitor for exact password
- Try forgetting network and reconnecting

### "Web interface won't load"
- Make sure you're connected to `yoRadio_pilot_setup`
- Try `http://192.168.4.1` (not https)
- Clear browser cache
- Try different browser

### "WiFi won't connect after config"
- Verify SSID and password are correct
- Check signal strength
- Use AP mode to reconfigure (will auto-start after timeout)

### "Changes don't take effect"
- Always click **Save Configuration** first
- Then click **Restart** 
- Wait 10 seconds for reboot

---

## 📱 Mobile Access

Works great on phones!
- Responsive design adapts to screen size
- Bookmark device IP for quick access
- Configure from anywhere on your network

---

## 🔒 Security Notes

- Web interface has **no password** by default
- Only accessible on your local network
- Consider adding router-level restrictions
- Change OTA password from default

---

## 📚 More Information

- **Full Documentation**: `WEB_INTERFACE.md`
- **Testing Guide**: `TESTING_GUIDE.md`
- **Technical Details**: `IMPLEMENTATION_SUMMARY.md`

---

## 🆘 Need Help?

1. Check serial monitor for errors
2. Enable DEBUG UART in web interface
3. Review documentation files
4. Check GitHub issues

---

## ✨ That's It!

You're ready to use yoRadio Pilot with web configuration!

**Next Steps:**
- Configure your radio IP addresses
- Adjust display brightness
- Set up deep sleep timeouts
- Add multiple radios if needed

**Enjoy! 🎵**
