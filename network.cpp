#include "network.h"
#include "display.h"
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

// Global network objects and state
WebSocketsClient webSocket;
WifiState wifiState = WIFI_CONNECTING;
bool wsConnected = false;
unsigned long lastWebSocketMessage = 0;
unsigned long wifiTimer = 0;
const unsigned long wifiTimeout = 8000;

// Multi-radio support - current radio stored in RTC memory (persists through deep sleep)
RTC_DATA_ATTR int currentRadio = 0;

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    DPRINTLN("WebSocket connected!");
    wsConnected = true;
    lastWebSocketMessage = millis();
    webSocket.sendTXT("getindex=1");
    return;
  }

  if (type == WStype_TEXT) {
    lastWebSocketMessage = millis();
    DPRINT("WebSocket message: ");
    DPRINTLN((char*)payload);

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) {
      DPRINT("JSON parse error: ");
      DPRINTLN(error.c_str());
      return;
    }

    if (doc.containsKey("payload")) {
      JsonArray arr = doc["payload"].as<JsonArray>();
      for (JsonObject obj : arr) {
        String id = obj["id"].as<String>();
        if (id == "nameset") stacja = obj["value"].as<String>();
        if (id == "meta") {
            String metaStr = obj["value"].as<String>();
            int sep = metaStr.indexOf(" - ");

            if (sep > 0) {
                // Jest " - ", czyli artysta i utwór
                wykonawca = metaStr.substring(0, sep);
                utwor = metaStr.substring(sep + 3);
            } else {
                // Brak " - ", czyli tylko utwór, artystę przesuwamy do utworu
                wykonawca = "";
                utwor = metaStr;
            }
        }
        if (id == "volume") volume = obj["value"].as<int>();
        if (id == "bitrate") bitrate = obj["value"].as<int>();
        if (id == "fmt") fmt = obj["value"].as<String>();
        if (id == "playerwrap") {
          playerwrap = obj["value"].as<String>();
          DPRINT("DEBUG: playerwrap = '");
          DPRINT(playerwrap);
          DPRINTLN("'");
        }
        if (id == "rssi") rssi = obj["value"].as<int>();
      }
    }

    if (wifiState == WIFI_OK) updateDisplay();
  }

  if (type == WStype_DISCONNECTED) {
    DPRINTLN("WebSocket disconnected!");
    wsConnected = false;
  }
}

void switchToRadio(int radioIndex) {
  // Validate radio index
  if (radioIndex < 0 || radioIndex >= NUM_RADIOS) {
    DPRINT("Invalid radio index: ");
    DPRINTLN(radioIndex);
    return;
  }

  DPRINT("Switching to radio ");
  DPRINT(radioIndex + 1);
  DPRINT(" (");
  DPRINT(RADIO_IPS[radioIndex]);
  DPRINT(") - ");
  DPRINTLN(RADIO_NAMES[radioIndex]);

  // Disconnect from current WebSocket
  if (webSocket.isConnected()) {
    webSocket.disconnect();
    wsConnected = false;
  }

  // Wait for clean disconnect
  delay(500);
  
  // Reset watchdog before long operation
  esp_task_wdt_reset();

  // Update current radio
  currentRadio = radioIndex;

  // Reset WebSocket message timer
  lastWebSocketMessage = millis();

  // Clear display state (station, artist, track)
  stacja = "";
  wykonawca = "";
  utwor = "";
  prev_stacja = "";
  prev_wykonawca = "";
  prev_utwor = "";

  // Reset scroll states
  for (int i = 0; i < 3; i++) {
    scrollStates[i].pos = 0;
    scrollStates[i].t_start = millis();
    scrollStates[i].t_last = millis();
    scrollStates[i].isMoving = false;
    scrollStates[i].scrolling = false;
    scrollStates[i].text = "";
    scrollStates[i].singleTextWidth = 0;
    scrollStates[i].suffixWidth = 0;
  }

  // Display radio name on screen during switch
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_WIDTH, 16, SSD1306_WHITE);
  
  String radioName = String(RADIO_NAMES[currentRadio]);
  int nameWidth = getPixelWidth5x7(radioName, 2);
  int nameX = (SCREEN_WIDTH - nameWidth) / 2;
  
  drawString5x7(nameX, 1, radioName, 2, SSD1306_BLACK);
  
  // Show IP address below name
  String ipText = String(RADIO_IPS[currentRadio]);
  int ipWidth = getPixelWidth5x7(ipText, 1);
  int ipX = (SCREEN_WIDTH - ipWidth) / 2;
  drawString5x7(ipX, 30, ipText, 1, SSD1306_WHITE);
  
  display.display();
  
  // Show radio name for configured time
  delay(RADIO_SWITCH_DISPLAY_TIME);

  // Connect to new radio
  DPRINT("Connecting to WebSocket at ");
  DPRINT(RADIO_IPS[currentRadio]);
  DPRINTLN(":80/ws");
  
  webSocket.begin(RADIO_IPS[currentRadio], 80, "/ws");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(WS_RECONNECT_INTERVAL_MS);  // Faster retry: 3000ms instead of default 5000ms
  
  // Reset watchdog after operation
  esp_task_wdt_reset();
}

void sendCommand(const char* cmd) {
  if (webSocket.isConnected()) {
    webSocket.sendTXT(cmd);
    DPRINT("Sent: ");
    DPRINTLN(cmd);
  } else {
    DPRINT("Attempt to send while WS disconnected: ");
    DPRINTLN(cmd);
  }
}
