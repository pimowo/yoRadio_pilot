#include <WiFi.h>
#include <WebSocketsClient.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>
#include "font5x7.h"

#define WIFI_SSID "pimowo"
#define WIFI_PASS "ckH59LRZQzCDQFiUgj"
#define IP_YORADIO "192.168.1.101"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

#define BTN_UP     7
#define BTN_RIGHT  4
#define BTN_CENTER 5
#define BTN_LEFT   6
#define BTN_DOWN   3

#define BATTERY_ADC_PIN 13
#define BATTERY_MIN_MV 3000
#define BATTERY_MAX_MV 4200

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebSocketsClient webSocket;

int batteryPercent = 100;
String meta = "";
int volume = 0;
int bitrate = 0;
int rssi = 0;
String playerwrap = "";
String stacja = "";
String wykonawca = "";
String utwor = "";

bool wsConnected = false;
unsigned long lastWebSocketMessage = 0;
unsigned long wsConnectStart = 0;
const unsigned long WS_CONNECT_TIMEOUT = 5000;
const unsigned long WS_MESSAGE_TIMEOUT = 10000;

unsigned long lastActivityTime = 0;
const unsigned long DEEP_SLEEP_TIMEOUT = 15000;

unsigned long lastButtonCheck = 0;
bool lastCenterState = HIGH;
bool lastLeftState = HIGH;
bool lastRightState = HIGH;
bool lastUpState = HIGH;
bool lastDownState = HIGH;

unsigned long volUpPressStart = 0;
unsigned long volDownPressStart = 0;
const unsigned long VOL_LONG_PRESS_THRESHOLD = 500;
const unsigned long VOL_REPEAT_INTERVAL = 200;

const unsigned char speakerIcon [] PROGMEM = {
  0b00011000, 0b00111000, 0b11111100, 0b11111100,
  0b11111100, 0b00111000, 0b00011000, 0b00001000
};

const unsigned char wifiErrorIcon[] PROGMEM = {
  0b00011000, 0b00100100, 0b01000010, 0b01000010,
  0b01000010, 0b00100100, 0b00011000, 0b00001000
};

const unsigned char batteryIcon[] PROGMEM = {
  0b01111111, 0b11111110,
  0b01000000, 0b00000010,
  0b11000000, 0b00000011,
  0b11000000, 0b00000011,
  0b11000000, 0b00000011,
  0b01000000, 0b00000010,
  0b01111111, 0b11111110,
  0b00000000, 0b00000000
};

const unsigned char wsErrorIcon[] PROGMEM = {
  0b00111100, 0b01000010, 0b10000001, 0b10011001,
  0b10011001, 0b10000001, 0b01000010, 0b00111100
};

enum WifiState { WIFI_CONNECTING, WIFI_ERROR, WIFI_OK };
WifiState wifiState = WIFI_CONNECTING;

unsigned long wifiTimer = 0;
const unsigned long wifiTimeout = 8000;

// ===== SCROLL CONFIGURATION =====
struct ScrollConfig {
  int left;
  int top;
  int fontsize;
  int width;
  unsigned long scrolldelay;
  int scrolldelta;
  unsigned long scrolltime;
};

const ScrollConfig scrollConfs[3] = {
  {2, 1, 2, SCREEN_WIDTH - 4, 10, 2, 1500},
  {0, 19, 2, SCREEN_WIDTH - 2, 10, 2, 1500},
  {0, 38, 1, SCREEN_WIDTH - 2, 10, 2, 1500}
};

// Stan scrolla dla każdej linii
struct ScrollState {
  int pos;
  unsigned long t_last;
  unsigned long t_start;
  bool scrolling;
  bool isMoving;
  String text;
  int singleTextWidth;
  int suffixWidth;
};

ScrollState scrollStates[3] = {0};

String prev_stacja = "";
String prev_wykonawca = "";
String prev_utwor = "";

const char* scrollSuffix = " * ";

// ===== SEKWENCYJNE PRZEWIJANIE =====
int activeScrollLine = 0;

// ===== UTF-8 POLISH =====
uint8_t mapUtf8Polish(uint16_t unicode) {
  switch (unicode) {
    case 0x0105: return 0xB8;
    case 0x0107: return 0xBD;
    case 0x0119: return 0xD6;
    case 0x0142: return 0xCF;
    case 0x0144: return 0xC0;
    case 0x00F3: return 0xBE;
    case 0x015B: return 0xCB;
    case 0x017A: return 0xBB;
    case 0x017C: return 0xB9;
    case 0x0104: return 0xB7;
    case 0x0106: return 0xC4;
    case 0x0118: return 0x90;
    case 0x0141: return 0xD0;
    case 0x0143: return 0xC1;
    case 0x00D3: return 0xBF;
    case 0x015A: return 0xCC;
    case 0x0179: return 0xBC;
    case 0x017B: return 0xBA;
    default: return 0;
  }
}

void drawChar5x7(int16_t x, int16_t y, uint8_t ch, uint16_t color = SSD1306_WHITE, uint8_t scale = 1) {
  uint16_t index = (uint16_t)ch * 5;
  for (uint8_t col = 0; col < 5; col++) {
    uint8_t line = pgm_read_byte_near(font + index + col);
    for (uint8_t row = 0; row < 8; row++) {
      if (line & (1 << row)) {
        if (scale == 1) {
          display.drawPixel(x + col, y + row, color);
        } else {
          display.fillRect(x + col * scale, y + row * scale, scale, scale, color);
        }
      }
    }
  }
}

void drawString5x7(int16_t x, int16_t y, const String &s, uint8_t scale = 1, uint16_t color = SSD1306_WHITE) {
  int16_t cx = x;
  const char* str = s.c_str();
  for (size_t i = 0; i < s.length();) {
    uint8_t c = (uint8_t)str[i];
    if (c < 128) {
      if (c >= 32) drawChar5x7(cx, y, c, color, scale);
      cx += (5 + 1) * scale;
      i++;
    } else {
      if ((c & 0xE0) == 0xC0 && i + 1 < s.length()) {
        uint8_t c2 = (uint8_t)str[i+1];
        uint16_t uni = ((c & 0x1F) << 6) | (c2 & 0x3F);
        uint8_t mapped = mapUtf8Polish(uni);
        if (mapped) drawChar5x7(cx, y, mapped, color, scale);
        else drawChar5x7(cx, y, '?', color, scale);
        cx += (5 + 1) * scale;
        i += 2;
      } else {
        i++;
      }
    }
  }
}

int getPixelWidth5x7(const String &s, uint8_t scale = 1) {
  int glyphs = 0;
  const char *str = s.c_str();
  for (size_t i = 0; i < s. length();) {
    uint8_t c = (uint8_t)str[i];
    if (c < 128) { glyphs++; i++; }
    else if ((c & 0xE0) == 0xC0 && i + 1 < s.length()) { glyphs++; i += 2; }
    else i++;
  }
  return glyphs * (5 + 1) * scale;
}

bool containsNonAscii(const String &s) {
  for (size_t i = 0; i < s.length(); i++) {
    if ((uint8_t)s[i] & 0x80) return true;
  }
  return false;
}

// ===== SCROLL FUNCTIONS =====

void prepareScroll(int line, const String& txt, int scale) {
  int singleWidth = getPixelWidth5x7(txt, scale);
  int availWidth = scrollConfs[line].width;
  
  // Sprawdzaj TYLKO szerokość samego tekstu
  bool needsScroll = singleWidth > availWidth;
  
  if (needsScroll) {
    // Tekst jest za długi - TERAZ dodaj sufiks do obliczenia
    int suffixWidth = getPixelWidth5x7(String(scrollSuffix), scale);
    scrollStates[line]. text = txt + String(scrollSuffix) + txt;
    scrollStates[line].singleTextWidth = singleWidth;
    scrollStates[line].suffixWidth = suffixWidth;
    scrollStates[line].scrolling = true;
  } else {
    // Tekst się mieści - BEZ suffiksa, bez obliczania jego szerokości
    scrollStates[line].text = txt;
    scrollStates[line].singleTextWidth = singleWidth;
    scrollStates[line]. suffixWidth = 0;  // Brak suffiksa
    scrollStates[line].scrolling = false;
  }
}

void updateScroll(int line) {
  unsigned long now = millis();
  auto& conf = scrollConfs[line];
  auto& state = scrollStates[line];
  
  // Jeśli nie przewija, ale jest aktywny - czekaj i przejdź do następnego
  if (!state.scrolling) {
    if (line == activeScrollLine) {
      unsigned long elapsed = now - state.t_start;
      if (elapsed >= conf.scrolltime) {
        activeScrollLine = (activeScrollLine + 1) % 3;
        scrollStates[activeScrollLine].t_start = now;
        scrollStates[activeScrollLine].t_last = now;
        scrollStates[activeScrollLine].pos = 0;
        scrollStates[activeScrollLine].isMoving = false;
      }
    }
    return;
  }
  
  if (line != activeScrollLine) return;
  
  unsigned long elapsed = now - state.t_start;
  
  if (state.pos == 0 && ! state.isMoving) {
    if (elapsed >= conf.scrolltime) {
      state.isMoving = true;
      state.t_last = now;
    }
    return;
  }
  
  if (now - state.t_last >= conf.scrolldelay) {
    state.pos -= conf.scrolldelta;
    
    int resetPos = -(state.singleTextWidth + state.suffixWidth);
    if (state.pos <= resetPos) {
      state. pos = 0;
      state.isMoving = false;
      state.t_start = now;
      
      activeScrollLine = (activeScrollLine + 1) % 3;
      scrollStates[activeScrollLine].t_start = now;
      scrollStates[activeScrollLine].t_last = now;
      scrollStates[activeScrollLine].pos = 0;
      scrollStates[activeScrollLine]. isMoving = false;
    }
    
    state.t_last = now;
  }
}

void drawScrollLine(int line, int scale) {
  auto& conf = scrollConfs[line];
  auto& state = scrollStates[line];
  
  int x = conf.left + state.pos;
  int y = conf.top;
  
  if (line == 0) {
    drawString5x7(x, y, state.text, scale, SSD1306_BLACK);
  } else {
    drawString5x7(x, y, state.text, scale, SSD1306_WHITE);
  }
}

int readBatteryPercent() {
  // Read ADC value from GPIO13 (ADC2_CH4)
  // Note: ADC2 may have issues when WiFi is active, but should work for most cases
  int adcValue = analogRead(BATTERY_ADC_PIN);
  
  // Convert ADC value to voltage (assuming 12-bit ADC: 0-4095 for 0-3.3V)
  // With 100k+100k voltage divider, actual battery voltage is 2x the measured voltage
  float measuredVoltage = (adcValue / 4095.0) * 3.3;
  float batteryVoltage = measuredVoltage * 2.0;
  int batteryMv = (int)(batteryVoltage * 1000);
  
  // Map battery voltage to percentage (3.0V = 0%, 4.2V = 100%)
  int percent = map(batteryMv, BATTERY_MIN_MV, BATTERY_MAX_MV, 0, 100);
  return constrain(percent, 0, 100);
}

void updateDisplay() {
  display.clearDisplay();

  if (wifiState == WIFI_CONNECTING) {
    static unsigned long lastAnim = 0;
    static int animStep = 1;
    if (millis() - lastAnim > 250) {
      lastAnim = millis();
      animStep++;
      if (animStep > 4) animStep = 1;
    }
    
    int barWidth = 4;
    int barSpacing = 2;
    int barHeights[4] = {2, 4, 6, 8};
    int totalWidth = animStep * (barWidth + barSpacing) - barSpacing;
    int startX = (SCREEN_WIDTH - totalWidth) / 2;
    int yLine = 15;
    
    for (int i = 0; i < animStep; i++) {
      int x = startX + i * (barWidth + barSpacing);
      display.fillRect(x, yLine + (8 - barHeights[i]), barWidth, barHeights[i], SSD1306_WHITE);
    }
    
    String ssidText = String(WIFI_SSID);
    int ssidWidth = getPixelWidth5x7(ssidText, 1);
    int ssidX = (SCREEN_WIDTH - ssidWidth) / 2;
    int ssidY = 35;
    
    drawString5x7(ssidX, ssidY, ssidText, 1, SSD1306_WHITE);
    
    display.display();
    return;
  }

  if (wifiState == WIFI_ERROR) {
    bool blink = (millis() % 1000 < 500);
    
    int iconX = (SCREEN_WIDTH - 8) / 2;
    int iconY = 10;
    if (blink) display.drawBitmap(iconX, iconY, wifiErrorIcon, 8, 8, SSD1306_WHITE);
    
    String errorText = "Brak WiFi";
    int errorWidth = getPixelWidth5x7(errorText, 1);
    int errorX = (SCREEN_WIDTH - errorWidth) / 2;
    int errorY = 30;
    drawString5x7(errorX, errorY, errorText, 1, SSD1306_WHITE);
    
    display.display();
    return;
  }

  // Check WebSocket connection (WiFi is OK but WebSocket failed or timed out)
  if (wifiState == WIFI_OK) {
    bool wsError = false;
    
    // Check if WebSocket never connected (timeout on initial connection)
    if (!wsConnected && wsConnectStart > 0 && (millis() - wsConnectStart) > WS_CONNECT_TIMEOUT) {
      wsError = true;
    }
    
    // Check if no message received for 10 seconds (during operation)
    if (wsConnected && lastWebSocketMessage > 0 && (millis() - lastWebSocketMessage) > WS_MESSAGE_TIMEOUT) {
      wsError = true;
    }
    
    if (wsError) {
      bool blink = (millis() % 1000 < 500);
      
      int iconX = (SCREEN_WIDTH - 8) / 2;
      int iconY = 10;
      if (blink) display.drawBitmap(iconX, iconY, wsErrorIcon, 8, 8, SSD1306_WHITE);
      
      String errorText = "Błąd połączenia z yoRadio";
      int errorWidth = getPixelWidth5x7(errorText, 1);
      int errorX = (SCREEN_WIDTH - errorWidth) / 2;
      int errorY = 30;
      drawString5x7(errorX, errorY, errorText, 1, SSD1306_WHITE);
      
      display.display();
      return;
    }
  }

  // MAIN SCREEN (WiFi OK)
  const int16_t lineHeight = 16;
  display.fillRect(0, 0, SCREEN_WIDTH, lineHeight, SSD1306_WHITE);

  // === PREPARE SCROLLS - TYLKO PRZY ZMIANIE =====
  if (stacja != prev_stacja) {
    prev_stacja = stacja;
    scrollStates[0].pos = 0;
    scrollStates[0].t_start = millis();
    scrollStates[0].t_last = millis();
    scrollStates[0].isMoving = false;
    prepareScroll(0, stacja, scrollConfs[0].fontsize);
  }

  if (wykonawca != prev_wykonawca) {
    prev_wykonawca = wykonawca;
    scrollStates[1].pos = 0;
    scrollStates[1]. t_start = millis();
    scrollStates[1].t_last = millis();
    scrollStates[1].isMoving = false;
    prepareScroll(1, wykonawca, scrollConfs[1].fontsize);
  }

  if (utwor != prev_utwor) {
    prev_utwor = utwor;
    scrollStates[2].pos = 0;
    scrollStates[2].t_start = millis();
    scrollStates[2].t_last = millis();
    scrollStates[2]. isMoving = false;
    prepareScroll(2, utwor, scrollConfs[2]. fontsize);
  }

  // === UPDATE SCROLLS ===
  updateScroll(0);
  updateScroll(1);
  updateScroll(2);

  // === DRAW ALL LINES ===
  drawScrollLine(0, scrollConfs[0].fontsize);
  drawScrollLine(1, scrollConfs[1]. fontsize);
  drawScrollLine(2, scrollConfs[2].fontsize);

  // BOTTOM LINE: RSSI, battery, volume, bitrate
  const int yLine = 52;
  int rssiX = 0;
  int barWidth = 3;
  int barSpacing = 2;
  int barHeights[4] = {2, 4, 6, 8};
  long rssiValue = WiFi.RSSI();
  int rssiPercent = constrain(map(rssiValue, -90, -30, 0, 100), 0, 100);
  int bars = map(rssiPercent, 0, 100, 0, 4);
  for (int i = 0; i < 4; i++) {
    int x = rssiX + i * (barWidth + barSpacing);
    if (i < bars)
      display.fillRect(x, yLine + (8 - barHeights[i]), barWidth, barHeights[i], SSD1306_WHITE);
    else
      display.drawFastHLine(x, yLine + (8 - barHeights[i]), barWidth, SSD1306_WHITE);
  }

  // Read and update battery level
  batteryPercent = readBatteryPercent();
  
  // Draw battery icon (16x8 pixels)
  int batX = 23;
  int batIconWidth = 16;
  int batIconHeight = 8;
  
  // Draw the battery outline
  display.drawBitmap(batX, yLine, batteryIcon, batIconWidth, batIconHeight, SSD1306_WHITE);
  
  // Fill the battery icon based on percentage
  // Inner area is 12 pixels wide (from x+2 to x+13), 6 pixels high (from y+1 to y+6)
  int fillWidth = (batteryPercent * 12) / 100;
  if (fillWidth > 0) {
    display.fillRect(batX + 2, yLine + 1, fillWidth, 6, SSD1306_WHITE);
  }

  int volX = 52;
  display.drawBitmap(volX, yLine, speakerIcon, 8, 8, SSD1306_WHITE);
  display.setCursor(volX + 10, yLine);
  display. setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.print(volume);

  display.setCursor(90, yLine);
  display. print(bitrate);
  display.print("kbs");

  display.display();
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.println("WebSocket connected!");
    wsConnected = true;
    lastWebSocketMessage = millis();
    webSocket.sendTXT("getindex=1");
    return;
  }

  if (type == WStype_TEXT) {
    Serial.print("WebSocket message: ");
    Serial.println((char*)payload);
    
    // Update timestamp for message timeout tracking
    lastWebSocketMessage = millis();
    
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
      return;
    }
    
    if (doc. containsKey("payload")) {
      JsonArray arr = doc["payload"]. as<JsonArray>();
      for (JsonObject obj : arr) {
        String id = obj["id"].as<String>();
        if (id == "nameset") stacja = obj["value"].as<String>();
        if (id == "meta") {
          String metaStr = obj["value"].as<String>();
          int sep = metaStr.indexOf(" - ");
          if (sep > 0) {
            wykonawca = metaStr.substring(0, sep);
            utwor = metaStr.substring(sep + 3);
          } else {
            wykonawca = "";
            utwor = metaStr;
          }
        }
        if (id == "volume") volume = obj["value"].as<int>();
        if (id == "bitrate") bitrate = obj["value"].as<int>();
        if (id == "playerwrap") playerwrap = obj["value"].as<String>();
        if (id == "rssi") rssi = obj["value"].as<int>();
      }
    }

    if (wifiState == WIFI_OK) updateDisplay();
  }
  
  if (type == WStype_DISCONNECTED) {
    Serial.println("WebSocket disconnected!");
    wsConnected = false;
  }
}

void sendCommand(const char* cmd) {
  if (webSocket.isConnected()) {
    webSocket.sendTXT(cmd);
    Serial.print("Sent: ");
    Serial.println(cmd);
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\nStarting YoRadio OLED Display.. .");

  // Configure deep sleep wakeup on BTN_CENTER (GPIO5, active LOW)
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_5, 0);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  
  // Initialize battery ADC pin
  pinMode(BATTERY_ADC_PIN, INPUT);
  analogReadResolution(12); // Set ADC resolution to 12 bits (0-4095)

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 failed"));
    for(;;);
  }

  display.clearDisplay();
  display. display();

  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiTimer = millis();
  wifiState = WIFI_CONNECTING;
  
  // Initialize activity timer
  lastActivityTime = millis();
}

void loop() {
  webSocket.loop();

  if (wifiState == WIFI_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      wifiState = WIFI_OK;
      Serial.print("Connecting to WebSocket at ");
      Serial.print(IP_YORADIO);
      Serial.println(":80/ws");
      webSocket.begin(IP_YORADIO, 80, "/ws");
      webSocket.onEvent(webSocketEvent);
      
      // Track when WebSocket connection attempt started
      wsConnectStart = millis();
      wsConnected = false;
    }
    if (millis() - wifiTimer > wifiTimeout) {
      Serial.println("WiFi connection timeout!");
      wifiState = WIFI_ERROR;
    }
  } else if (wifiState == WIFI_OK) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected!");
      wifiState = WIFI_CONNECTING;
      wifiTimer = millis();
      wsConnected = false;
    }
  } else if (wifiState == WIFI_ERROR) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial. println("WiFi reconnected!");
      wifiState = WIFI_OK;
    }
  }

  updateDisplay();

  // Check for deep sleep timeout (15 seconds of inactivity)
  if ((millis() - lastActivityTime) > DEEP_SLEEP_TIMEOUT) {
    Serial.println("Entering deep sleep due to inactivity...");
    display.clearDisplay();
    display.display();
    esp_deep_sleep_start();
  }

  if (millis() - lastButtonCheck > 120) {
    lastButtonCheck = millis();

    bool curUp = digitalRead(BTN_UP);
    bool curRight = digitalRead(BTN_RIGHT);
    bool curCenter = digitalRead(BTN_CENTER);
    bool curLeft = digitalRead(BTN_LEFT);
    bool curDown = digitalRead(BTN_DOWN);

    // Reset activity timer on any button press
    if (curUp == LOW || curDown == LOW || curCenter == LOW || curLeft == LOW || curRight == LOW) {
      lastActivityTime = millis();
    }

    // VOL+ with long press support
    if (curUp == LOW) {
      if (lastUpState == HIGH) {
        // Initial press - send first command and start tracking
        sendCommand("volp=1");
        volUpPressStart = millis();
      } else {
        // Button is held - check for repeat
        unsigned long pressDuration = millis() - volUpPressStart;
        if (pressDuration > VOL_LONG_PRESS_THRESHOLD) {
          // Long press detected - send repeat commands
          static unsigned long lastVolUpRepeat = 0;
          if (millis() - lastVolUpRepeat >= VOL_REPEAT_INTERVAL) {
            sendCommand("volp=1");
            lastVolUpRepeat = millis();
          }
        }
      }
    }

    // VOL- with long press support
    if (curDown == LOW) {
      if (lastDownState == HIGH) {
        // Initial press - send first command and start tracking
        sendCommand("volm=1");
        volDownPressStart = millis();
      } else {
        // Button is held - check for repeat
        unsigned long pressDuration = millis() - volDownPressStart;
        if (pressDuration > VOL_LONG_PRESS_THRESHOLD) {
          // Long press detected - send repeat commands
          static unsigned long lastVolDownRepeat = 0;
          if (millis() - lastVolDownRepeat >= VOL_REPEAT_INTERVAL) {
            sendCommand("volm=1");
            lastVolDownRepeat = millis();
          }
        }
      }
    }

    if (curCenter == LOW && lastCenterState == HIGH) sendCommand("toggle=1");
    if (curLeft == LOW && lastLeftState == HIGH) sendCommand("prev=1");
    if (curRight == LOW && lastRightState == HIGH) sendCommand("next=1");

    lastCenterState = curCenter;
    lastLeftState = curLeft;
    lastRightState = curRight;
    lastUpState = curUp;
    lastDownState = curDown;
  }
}
