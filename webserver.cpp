#include "webserver.h"
#include "config.h"
#include "version.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <esp_system.h>
#include <Update.h>

AsyncWebServer webServer(80);
bool apMode = false;

// Safe string copy with guaranteed null termination
static void safe_strncpy(char* dest, const char* src, size_t size) {
  if (size > 0) {
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
  }
}

// HTML page with inline CSS and JavaScript
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>yoRadio Pilot - Konfiguracja</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: Arial, sans-serif;
            background: #1a1a1a;
            color: #e0e0e0;
            padding: 10px;
            line-height: 1.6;
        }
        .container { max-width: 800px; margin: 0 auto; }
        h1 {
            text-align: center;
            color: #4CAF50;
            margin: 20px 0;
            font-size: 24px;
        }
        .section {
            background: #2a2a2a;
            padding: 20px;
            margin: 15px 0;
            border-radius: 8px;
            border: 1px solid #3a3a3a;
        }
        .section h2 {
            color: #4CAF50;
            margin-bottom: 15px;
            font-size: 18px;
            border-bottom: 1px solid #3a3a3a;
            padding-bottom: 10px;
        }
        .form-group {
            margin-bottom: 15px;
        }
        label {
            display: block;
            margin-bottom: 5px;
            color: #b0b0b0;
            font-size: 14px;
        }
        input[type="text"],
        input[type="password"],
        input[type="number"],
        select {
            width: 100%;
            padding: 10px;
            background: #1a1a1a;
            border: 1px solid #3a3a3a;
            border-radius: 4px;
            color: #e0e0e0;
            font-size: 14px;
        }
        input[type="checkbox"] {
            width: 20px;
            height: 20px;
            margin-right: 10px;
            vertical-align: middle;
        }
        .checkbox-label {
            display: inline-block;
            vertical-align: middle;
        }
        .radio-list {
            border: 1px solid #3a3a3a;
            padding: 15px;
            border-radius: 4px;
            margin-bottom: 15px;
            background: #1a1a1a;
        }
        .radio-item {
            display: grid;
            grid-template-columns: 1fr 2fr auto;
            gap: 10px;
            margin-bottom: 10px;
            align-items: center;
        }
        .btn {
            padding: 12px 20px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-size: 14px;
            margin: 5px;
            transition: opacity 0.2s;
        }
        .btn:hover { opacity: 0.8; }
        .btn-primary {
            background: #4CAF50;
            color: white;
        }
        .btn-danger {
            background: #f44336;
            color: white;
        }
        .btn-warning {
            background: #ff9800;
            color: white;
        }
        .btn-info {
            background: #2196F3;
            color: white;
        }
        .status-box {
            background: #1a1a1a;
            padding: 15px;
            border-radius: 4px;
            border: 1px solid #3a3a3a;
            margin-bottom: 10px;
        }
        .status-item {
            display: flex;
            justify-content: space-between;
            margin: 8px 0;
            font-size: 14px;
        }
        .status-label {
            color: #b0b0b0;
        }
        .status-value {
            color: #4CAF50;
            font-weight: bold;
        }
        .button-group {
            display: flex;
            flex-wrap: wrap;
            justify-content: center;
            margin-top: 20px;
        }
        .message {
            padding: 15px;
            margin: 15px 0;
            border-radius: 4px;
            text-align: center;
            display: none;
        }
        .message.success {
            background: #4CAF50;
            color: white;
        }
        .message.error {
            background: #f44336;
            color: white;
        }
        @media (max-width: 600px) {
            .radio-item {
                grid-template-columns: 1fr;
            }
            .btn {
                width: 100%;
                margin: 5px 0;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>⚙️ yoRadio Pilot - Konfiguracja</h1>
        
        <div id="message" class="message"></div>
        
        <div class="section">
            <h2>📊 Status</h2>
            <div class="status-box">
                <div class="status-item">
                    <span class="status-label">Wersja firmware:</span>
                    <span class="status-value" id="version">-</span>
                </div>
                <div class="status-item">
                    <span class="status-label">WiFi RSSI:</span>
                    <span class="status-value" id="rssi">-</span>
                </div>
                <div class="status-item">
                    <span class="status-label">IP:</span>
                    <span class="status-value" id="ip">-</span>
                </div>
                <div class="status-item">
                    <span class="status-label">Wolna pamięć:</span>
                    <span class="status-value" id="heap">-</span>
                </div>
                <div class="status-item">
                    <span class="status-label">Uptime:</span>
                    <span class="status-value" id="uptime">-</span>
                </div>
            </div>
        </div>
        
        <form id="configForm">
            <div class="section">
                <h2>📡 WiFi</h2>
                <div class="form-group">
                    <label>SSID sieci:</label>
                    <input type="text" id="wifi_ssid" name="wifi_ssid" required>
                </div>
                <div class="form-group">
                    <label>Hasło WiFi:</label>
                    <input type="password" id="wifi_pass" name="wifi_pass">
                </div>
                <div class="form-group">
                    <input type="checkbox" id="use_dhcp" name="use_dhcp">
                    <label class="checkbox-label" for="use_dhcp">Użyj DHCP (automatyczne IP)</label>
                </div>
                <div id="static_ip_fields">
                    <div class="form-group">
                        <label>Statyczne IP:</label>
                        <input type="text" id="static_ip" name="static_ip" placeholder="192.168.1.111">
                    </div>
                    <div class="form-group">
                        <label>Gateway:</label>
                        <input type="text" id="gateway" name="gateway" placeholder="192.168.1.1">
                    </div>
                    <div class="form-group">
                        <label>Maska podsieci:</label>
                        <input type="text" id="subnet" name="subnet" placeholder="255.255.255.0">
                    </div>
                    <div class="form-group">
                        <label>DNS 1:</label>
                        <input type="text" id="dns1" name="dns1" placeholder="192.168.1.1">
                    </div>
                    <div class="form-group">
                        <label>DNS 2:</label>
                        <input type="text" id="dns2" name="dns2" placeholder="8.8.8.8">
                    </div>
                </div>
            </div>
            
            <div class="section">
                <h2>📻 Radia (yoRadio)</h2>
                <div class="form-group">
                    <label>Liczba radiów (1-5):</label>
                    <input type="number" id="num_radios" name="num_radios" min="1" max="5" value="1">
                </div>
                <div id="radios_list"></div>
                <div class="form-group">
                    <label>Domyślne radio:</label>
                    <select id="default_radio" name="default_radio">
                        <option value="0">Radio 1</option>
                    </select>
                </div>
            </div>
            
            <div class="section">
                <h2>⏱️ Timeouty</h2>
                <div class="form-group">
                    <label>Deep sleep podczas odtwarzania (sekundy):</label>
                    <input type="number" id="deep_sleep_timeout" name="deep_sleep_timeout" min="10" max="3600" value="60">
                </div>
                <div class="form-group">
                    <label>Deep sleep gdy zatrzymany (sekundy):</label>
                    <input type="number" id="deep_sleep_timeout_stopped" name="deep_sleep_timeout_stopped" min="1" max="600" value="5">
                </div>
                <div class="form-group">
                    <label>Długość long-press (milisekundy):</label>
                    <input type="number" id="long_press_time" name="long_press_time" min="500" max="5000" step="100" value="2000">
                </div>
            </div>
            
            <div class="section">
                <h2>🔄 OTA</h2>
                <div class="form-group">
                    <label>Hostname OTA:</label>
                    <input type="text" id="ota_hostname" name="ota_hostname" value="yoRadio_pilot">
                </div>
                <div class="form-group">
                    <label>Hasło OTA:</label>
                    <input type="password" id="ota_password" name="ota_password">
                </div>
            </div>
            
            <div class="section">
                <h2>🖥️ Display</h2>
                <div class="form-group">
                    <label>Jasność OLED (0-15):</label>
                    <input type="number" id="oled_brightness" name="oled_brightness" min="0" max="15" value="10">
                </div>
                <div class="form-group">
                    <label>Częstotliwość odświeżania (ms):</label>
                    <input type="number" id="display_refresh_rate" name="display_refresh_rate" min="20" max="200" step="10" value="50">
                </div>
            </div>
            
            <div class="section">
                <h2>🐛 Debug</h2>
                <div class="form-group">
                    <input type="checkbox" id="debug_uart" name="debug_uart">
                    <label class="checkbox-label" for="debug_uart">Włącz DEBUG UART</label>
                </div>
            </div>
            
            <div class="button-group">
                <button type="submit" class="btn btn-primary">💾 Zapisz konfigurację</button>
                <button type="button" class="btn btn-warning" onclick="restartDevice()">🔄 Restart</button>
                <button type="button" class="btn btn-danger" onclick="resetToDefaults()">⚠️ Reset do domyślnych</button>
                <button type="button" class="btn btn-info" onclick="window.location.href='/update'">⬆️ Aktualizuj firmware</button>
            </div>
        </form>
    </div>
    
    <script>
        // Load configuration and status on page load
        window.addEventListener('DOMContentLoaded', function() {
            loadConfig();
            loadStatus();
            setInterval(loadStatus, 2000); // Update status every 2 seconds
        });
        
        // Toggle DHCP fields
        document.getElementById('use_dhcp').addEventListener('change', function() {
            document.getElementById('static_ip_fields').style.display = this.checked ? 'none' : 'block';
        });
        
        // Update number of radios
        document.getElementById('num_radios').addEventListener('change', function() {
            updateRadiosList();
            updateDefaultRadioSelect();
        });
        
        function showMessage(text, type) {
            const msg = document.getElementById('message');
            msg.textContent = text;
            msg.className = 'message ' + type;
            msg.style.display = 'block';
            setTimeout(() => {
                msg.style.display = 'none';
            }, 5000);
        }
        
        function updateRadiosList() {
            const numRadios = parseInt(document.getElementById('num_radios').value);
            const container = document.getElementById('radios_list');
            container.innerHTML = '';
            
            for (let i = 0; i < numRadios; i++) {
                const div = document.createElement('div');
                div.className = 'radio-list';
                div.innerHTML = `
                    <div class="radio-item">
                        <label>Radio ${i + 1} - Nazwa:</label>
                        <input type="text" id="radio_name_${i}" placeholder="Radio ${i + 1}" value="Radio ${i + 1}">
                    </div>
                    <div class="radio-item">
                        <label>Radio ${i + 1} - IP:</label>
                        <input type="text" id="radio_ip_${i}" placeholder="192.168.1.10${i + 1}" required>
                    </div>
                `;
                container.appendChild(div);
            }
        }
        
        function updateDefaultRadioSelect() {
            const numRadios = parseInt(document.getElementById('num_radios').value);
            const select = document.getElementById('default_radio');
            const currentValue = select.value;
            select.innerHTML = '';
            
            for (let i = 0; i < numRadios; i++) {
                const option = document.createElement('option');
                option.value = i;
                option.textContent = `Radio ${i + 1}`;
                select.appendChild(option);
            }
            
            if (currentValue < numRadios) {
                select.value = currentValue;
            }
        }
        
        function loadConfig() {
            fetch('/api/config')
                .then(response => response.json())
                .then(config => {
                    // WiFi
                    document.getElementById('wifi_ssid').value = config.wifi_ssid || '';
                    document.getElementById('wifi_pass').value = config.wifi_pass || '';
                    document.getElementById('use_dhcp').checked = config.use_dhcp || false;
                    document.getElementById('static_ip').value = config.static_ip || '';
                    document.getElementById('gateway').value = config.gateway || '';
                    document.getElementById('subnet').value = config.subnet || '';
                    document.getElementById('dns1').value = config.dns1 || '';
                    document.getElementById('dns2').value = config.dns2 || '';
                    
                    document.getElementById('static_ip_fields').style.display = 
                        config.use_dhcp ? 'none' : 'block';
                    
                    // Radios
                    document.getElementById('num_radios').value = config.num_radios || 1;
                    updateRadiosList();
                    
                    for (let i = 0; i < 5; i++) {
                        const nameInput = document.getElementById(`radio_name_${i}`);
                        const ipInput = document.getElementById(`radio_ip_${i}`);
                        if (nameInput && config.radio_names && config.radio_names[i]) {
                            nameInput.value = config.radio_names[i];
                        }
                        if (ipInput && config.radio_ips && config.radio_ips[i]) {
                            ipInput.value = config.radio_ips[i];
                        }
                    }
                    
                    updateDefaultRadioSelect();
                    document.getElementById('default_radio').value = config.default_radio || 0;
                    
                    // Timeouts
                    document.getElementById('deep_sleep_timeout').value = config.deep_sleep_timeout || 60;
                    document.getElementById('deep_sleep_timeout_stopped').value = config.deep_sleep_timeout_stopped || 5;
                    document.getElementById('long_press_time').value = config.long_press_time || 2000;
                    
                    // OTA
                    document.getElementById('ota_hostname').value = config.ota_hostname || 'yoRadio_pilot';
                    document.getElementById('ota_password').value = config.ota_password || '';
                    
                    // Display
                    document.getElementById('oled_brightness').value = config.oled_brightness || 10;
                    document.getElementById('display_refresh_rate').value = config.display_refresh_rate || 50;
                    
                    // Debug
                    document.getElementById('debug_uart').checked = config.debug_uart || false;
                })
                .catch(error => {
                    console.error('Error loading config:', error);
                    showMessage('Błąd wczytywania konfiguracji', 'error');
                });
        }
        
        function loadStatus() {
            fetch('/api/status')
                .then(response => response.json())
                .then(status => {
                    document.getElementById('version').textContent = status.version || '-';
                    document.getElementById('rssi').textContent = status.rssi ? status.rssi + ' dBm' : '-';
                    document.getElementById('ip').textContent = status.ip || '-';
                    document.getElementById('heap').textContent = status.heap ? 
                        Math.round(status.heap / 1024) + ' KB' : '-';
                    document.getElementById('uptime').textContent = status.uptime || '-';
                })
                .catch(error => console.error('Error loading status:', error));
        }
        
        document.getElementById('configForm').addEventListener('submit', function(e) {
            e.preventDefault();
            
            const config = {
                wifi_ssid: document.getElementById('wifi_ssid').value,
                wifi_pass: document.getElementById('wifi_pass').value,
                use_dhcp: document.getElementById('use_dhcp').checked,
                static_ip: document.getElementById('static_ip').value,
                gateway: document.getElementById('gateway').value,
                subnet: document.getElementById('subnet').value,
                dns1: document.getElementById('dns1').value,
                dns2: document.getElementById('dns2').value,
                num_radios: parseInt(document.getElementById('num_radios').value),
                default_radio: parseInt(document.getElementById('default_radio').value),
                radio_ips: [],
                radio_names: [],
                deep_sleep_timeout: parseInt(document.getElementById('deep_sleep_timeout').value),
                deep_sleep_timeout_stopped: parseInt(document.getElementById('deep_sleep_timeout_stopped').value),
                long_press_time: parseInt(document.getElementById('long_press_time').value),
                ota_hostname: document.getElementById('ota_hostname').value,
                ota_password: document.getElementById('ota_password').value,
                oled_brightness: parseInt(document.getElementById('oled_brightness').value),
                display_refresh_rate: parseInt(document.getElementById('display_refresh_rate').value),
                debug_uart: document.getElementById('debug_uart').checked
            };
            
            // Collect radio IPs and names
            for (let i = 0; i < 5; i++) {
                const nameInput = document.getElementById(`radio_name_${i}`);
                const ipInput = document.getElementById(`radio_ip_${i}`);
                config.radio_names.push(nameInput ? nameInput.value : '');
                config.radio_ips.push(ipInput ? ipInput.value : '');
            }
            
            fetch('/api/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(config)
            })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    showMessage('✅ Konfiguracja zapisana! Restart wymagany.', 'success');
                } else {
                    showMessage('❌ Błąd zapisu konfiguracji: ' + (data.message || 'Unknown error'), 'error');
                }
            })
            .catch(error => {
                showMessage('❌ Błąd połączenia: ' + error.message, 'error');
            });
        });
        
        function restartDevice() {
            if (confirm('Czy na pewno chcesz zrestartować urządzenie?')) {
                fetch('/api/restart', { method: 'POST' })
                    .then(() => {
                        showMessage('🔄 Urządzenie restartuje się...', 'success');
                        setTimeout(() => {
                            window.location.reload();
                        }, 5000);
                    })
                    .catch(error => {
                        showMessage('❌ Błąd restartu: ' + error.message, 'error');
                    });
            }
        }
        
        function resetToDefaults() {
            if (confirm('Czy na pewno chcesz przywrócić ustawienia domyślne? Ta operacja usunie zapisaną konfigurację i zrestartuje urządzenie.')) {
                fetch('/api/reset', { method: 'POST' })
                    .then(() => {
                        showMessage('⚠️ Resetowanie do domyślnych ustawień...', 'success');
                        setTimeout(() => {
                            window.location.reload();
                        }, 5000);
                    })
                    .catch(error => {
                        showMessage('❌ Błąd resetowania: ' + error.message, 'error');
                    });
            }
        }
        
        // Initialize on load
        updateRadiosList();
        updateDefaultRadioSelect();
    </script>
</body>
</html>
)rawliteral";

String formatUptime(unsigned long ms) {
  unsigned long seconds = ms / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  
  hours = hours % 24;
  minutes = minutes % 60;
  seconds = seconds % 60;
  
  String uptime = "";
  if (days > 0) uptime += String(days) + "d ";
  if (hours > 0) uptime += String(hours) + "h ";
  if (minutes > 0) uptime += String(minutes) + "m ";
  uptime += String(seconds) + "s";
  
  return uptime;
}

void setupWebServer() {
  // Serve main page
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });
  
  // API: Get configuration
  webServer.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<2048> doc;
    
    doc["wifi_ssid"] = config.wifi_ssid;
    doc["wifi_pass"] = config.wifi_pass;
    doc["use_dhcp"] = config.use_dhcp;
    doc["static_ip"] = config.static_ip;
    doc["gateway"] = config.gateway;
    doc["subnet"] = config.subnet;
    doc["dns1"] = config.dns1;
    doc["dns2"] = config.dns2;
    
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
    
    doc["deep_sleep_timeout"] = config.deep_sleep_timeout;
    doc["deep_sleep_timeout_stopped"] = config.deep_sleep_timeout_stopped;
    doc["long_press_time"] = config.long_press_time;
    
    doc["ota_hostname"] = config.ota_hostname;
    doc["ota_password"] = config.ota_password;
    
    doc["oled_brightness"] = config.oled_brightness;
    doc["display_refresh_rate"] = config.display_refresh_rate;
    
    doc["debug_uart"] = config.debug_uart;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API: Save configuration
  webServer.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      StaticJsonDocument<2048> doc;
      DeserializationError error = deserializeJson(doc, data, len);
      
      if (error) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
      }
      
      // Update config structure
      if (doc.containsKey("wifi_ssid"))
        safe_strncpy(config.wifi_ssid, doc["wifi_ssid"], sizeof(config.wifi_ssid));
      if (doc.containsKey("wifi_pass"))
        safe_strncpy(config.wifi_pass, doc["wifi_pass"], sizeof(config.wifi_pass));
      if (doc.containsKey("use_dhcp"))
        config.use_dhcp = doc["use_dhcp"];
      if (doc.containsKey("static_ip"))
        safe_strncpy(config.static_ip, doc["static_ip"], sizeof(config.static_ip));
      if (doc.containsKey("gateway"))
        safe_strncpy(config.gateway, doc["gateway"], sizeof(config.gateway));
      if (doc.containsKey("subnet"))
        safe_strncpy(config.subnet, doc["subnet"], sizeof(config.subnet));
      if (doc.containsKey("dns1"))
        safe_strncpy(config.dns1, doc["dns1"], sizeof(config.dns1));
      if (doc.containsKey("dns2"))
        safe_strncpy(config.dns2, doc["dns2"], sizeof(config.dns2));
      
      if (doc.containsKey("num_radios"))
        config.num_radios = doc["num_radios"];
      if (doc.containsKey("default_radio"))
        config.default_radio = doc["default_radio"];
      
      if (doc.containsKey("radio_ips")) {
        JsonArray ips = doc["radio_ips"].as<JsonArray>();
        int i = 0;
        for (JsonVariant ip : ips) {
          if (i < 5) {
            safe_strncpy(config.radio_ips[i], ip.as<const char*>(), sizeof(config.radio_ips[i]));
            i++;
          }
        }
      }
      
      if (doc.containsKey("radio_names")) {
        JsonArray names = doc["radio_names"].as<JsonArray>();
        int i = 0;
        for (JsonVariant name : names) {
          if (i < 5) {
            safe_strncpy(config.radio_names[i], name.as<const char*>(), sizeof(config.radio_names[i]));
            i++;
          }
        }
      }
      
      if (doc.containsKey("deep_sleep_timeout"))
        config.deep_sleep_timeout = doc["deep_sleep_timeout"];
      if (doc.containsKey("deep_sleep_timeout_stopped"))
        config.deep_sleep_timeout_stopped = doc["deep_sleep_timeout_stopped"];
      if (doc.containsKey("long_press_time"))
        config.long_press_time = doc["long_press_time"];
      
      if (doc.containsKey("ota_hostname"))
        safe_strncpy(config.ota_hostname, doc["ota_hostname"], sizeof(config.ota_hostname));
      if (doc.containsKey("ota_password"))
        safe_strncpy(config.ota_password, doc["ota_password"], sizeof(config.ota_password));
      
      if (doc.containsKey("oled_brightness"))
        config.oled_brightness = doc["oled_brightness"];
      if (doc.containsKey("display_refresh_rate"))
        config.display_refresh_rate = doc["display_refresh_rate"];
      
      if (doc.containsKey("debug_uart"))
        config.debug_uart = doc["debug_uart"];
      
      // Save to SPIFFS
      saveConfig();
      
      request->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration saved\"}");
    });
  
  // API: Get status
  webServer.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<512> doc;
    
    doc["version"] = FIRMWARE_VERSION;
    doc["rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime"] = formatUptime(millis());
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API: Restart device
  webServer.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Restarting...\"}");
    delay(500);
    ESP.restart();
  });
  
  // API: Reset to defaults
  webServer.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    SPIFFS.remove("/config.json");
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Reset to defaults...\"}");
    delay(500);
    ESP.restart();
  });
  
  // Start server
  webServer.begin();
  Serial.println("Web server started on port 80");
}

void startAPMode() {
  Serial.println("Starting Access Point mode...");
  
  WiFi.mode(WIFI_AP);
  // AP Password: Generated from ESP32 MAC address for uniqueness and security
  // Each device has a different password, making it harder to guess
  String apPassword = "yoRadio" + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
  WiFi.softAP("yoRadio_pilot_setup", apPassword.c_str());
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  Serial.print("AP Password: ");
  Serial.println(apPassword);
  
  apMode = true;
  
  // Setup web server
  setupWebServer();
}

bool isAPMode() {
  return apMode;
}
