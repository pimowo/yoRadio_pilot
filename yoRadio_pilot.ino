#include <WiFi.h>
#include <WebSocketsClient.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include "font5x7.h"

// Sprawdzenie czy stała SSD1306_SETCONTRAST jest zdefiniowana w bibliotece
#ifndef SSD1306_SETCONTRAST
#define SSD1306_SETCONTRAST 0x81  // Komenda kontrastu SSD1306 (z datasheet)
#endif

//==================================================================================================
// sieć
#define WIFI_SSID "pimowo"             // sieć 
#define WIFI_PASS "ckH59LRZQzCDQFiUgj" // hasło sieci
// yoRadio
#define IP_YORADIO "192.168.1.101"     // IP yoRadio
// uśpienie
#define DEEP_SLEEP_TIMEOUT_SEC 20      // sekundy bezczynności przed deep sleep
// klawiattura
#define BTN_UP     7 // pin GÓRA
#define BTN_RIGHT  4 // pin PRAWO
#define BTN_CENTER 5 // pin OK
#define BTN_LEFT   6 // pin LEWO 
#define BTN_DOWN   3 // pin DÓŁ
//==================================================================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_BRIGHTNESS_MULTIPLIER 16  // Mnożnik x16 daje wygodną skalę 0-15 dla użytkownika
#define OLED_BRIGHTNESS 10             // Jasność OLED: 0 (min) do 15 (max), domyślnie 10

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
const unsigned long WS_TIMEOUT_MS = 10000;  // 10 sekund timeout

unsigned long lastButtonCheck = 0;
unsigned long lastActivityTime = 0;
bool lastCenterState = HIGH;
bool lastLeftState = HIGH;
bool lastRightState = HIGH;
bool lastUpState = HIGH;
bool lastDownState = HIGH;

const unsigned char speakerIcon [] PROGMEM = {
  0b00011000, 0b00111000, 0b11111100, 0b11111100,
  0b11111100, 0b00111000, 0b00011000, 0b00001000
};

const unsigned char wifiErrorIcon[] PROGMEM = {
  0b00011000, 0b00100100, 0b01000010, 0b01000010,
  0b01000010, 0b00100100, 0b00011000, 0b00001000
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
    scrollStates[line].text = txt + String(scrollSuffix) + txt;
    scrollStates[line].singleTextWidth = singleWidth;
    scrollStates[line].suffixWidth = suffixWidth;
    scrollStates[line].scrolling = true;
  } else {
    // Tekst się mieści - BEZ suffiksa, bez obliczania jego szerokości
    scrollStates[line].text = txt;
    scrollStates[line].singleTextWidth = singleWidth;
    scrollStates[line].suffixWidth = 0;  // Brak suffiksa
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
      scrollStates[activeScrollLine].isMoving = false;
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

  // Sprawdź status WebSocket
  bool wsError = !wsConnected || ((millis() - lastWebSocketMessage) > WS_TIMEOUT_MS);

  if (wsError) {
    String errorText = "Brak yoRadio!";
    int errorWidth = getPixelWidth5x7(errorText, 1);
    int errorX = (SCREEN_WIDTH - errorWidth) / 2;
    int errorY = 30;
    drawString5x7(errorX, errorY, errorText, 1, SSD1306_WHITE);
    
    display.display();
    return;  // Wyjdź z funkcji, nie rysuj reszty
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
    scrollStates[1].t_start = millis();
    scrollStates[1].t_last = millis();
    scrollStates[1].isMoving = false;
    prepareScroll(1, wykonawca, scrollConfs[1].fontsize);
  }

  if (utwor != prev_utwor) {
    prev_utwor = utwor;
    scrollStates[2].pos = 0;
    scrollStates[2].t_start = millis();
    scrollStates[2].t_last = millis();
    scrollStates[2].isMoving = false;
    prepareScroll(2, utwor, scrollConfs[2]. fontsize);
  }

  // === UPDATE SCROLLS ===
  updateScroll(0);
  updateScroll(1);
  updateScroll(2);

  // === DRAW ALL LINES ===
  drawScrollLine(0, scrollConfs[0].fontsize);
  drawScrollLine(1, scrollConfs[1].fontsize);
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

  int batX = 23;
  int batWidth = 20;
  int batHeight = 8;
  display.drawRect(batX, yLine, batWidth, batHeight, SSD1306_WHITE);
  display.fillRect(batX + batWidth, yLine + 2, 2, batHeight - 4, SSD1306_WHITE);
  int fillWidth = (batteryPercent * (batWidth - 2)) / 100;
  if (fillWidth > 0) display.fillRect(batX + 1, yLine + 1, fillWidth, batHeight - 2, SSD1306_WHITE);

  int volX = 52;
  display.drawBitmap(volX, yLine, speakerIcon, 8, 8, SSD1306_WHITE);
  display.setCursor(volX + 10, yLine);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.print(volume);

  display.setCursor(90, yLine);
  display.print(bitrate);
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
    lastWebSocketMessage = millis();
    Serial.print("WebSocket message: ");
    Serial.println((char*)payload);
    
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
      return;
    }
    
    if (doc.containsKey("payload")) {
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

void enterDeepSleep() {
  Serial.println("Clearing display and powering down...");
  display.clearDisplay();
  display.display();
  display.ssd1306_command(0xAE);  // Wyłącz OLED
  
  Serial.println("Powering down WiFi...");
  webSocket.disconnect();
  WiFi.disconnect(true);  // wyłącz WiFi radio
  WiFi.mode(WIFI_OFF);
  
  Serial.println("Entering deep sleep...");
  
  // GPIO5 (BTN_CENTER) = pin do wybudzenia
  // 0 = LOW trigger (przycisk zwiera GND, więc LOW = naciśnięty)
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_5, 0);
  
  Serial.flush();
  
  // Przejdź w deep sleep
  esp_deep_sleep_start();
  // Kod poniżej NIE ZOSTANIE wykonany!
  // ESP32 się wybudzi i zrobi pełny boot
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\nStarting YoRadio OLED Display.. .");

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 failed"));
    for(;;);
  }

  // Ustawienie jasności OLED (zakres SSD1306: 0-255, używamy 0-240, domyślnie ~143)
  // UWAGA: Adafruit_SSD1306 nie ma metody setContrast() ani contrast()
  // Prawidłowy sposób to użycie ssd1306_command() z SSD1306_SETCONTRAST (0x81)
  display.ssd1306_command(SSD1306_SETCONTRAST);
  uint8_t brightness = min(255, OLED_BRIGHTNESS * OLED_BRIGHTNESS_MULTIPLIER);
  display.ssd1306_command(brightness);

  display.clearDisplay();
  display.display();

  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiTimer = millis();
  wifiState = WIFI_CONNECTING;
  
  // Inicjalizacja timerów
  lastActivityTime = millis();
  lastWebSocketMessage = millis();
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
    }
  } else if (wifiState == WIFI_ERROR) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi reconnected!");
      wifiState = WIFI_OK;
    }
  }

  updateDisplay();

  if (millis() - lastButtonCheck > 120) {
    lastButtonCheck = millis();

    bool curUp = digitalRead(BTN_UP);
    bool curRight = digitalRead(BTN_RIGHT);
    bool curCenter = digitalRead(BTN_CENTER);
    bool curLeft = digitalRead(BTN_LEFT);
    bool curDown = digitalRead(BTN_DOWN);

    bool anyButtonPressed = false;

    if (curUp == LOW) {
      sendCommand("volp=1");
      anyButtonPressed = true;
    }
    if (curDown == LOW) {
      sendCommand("volm=1");
      anyButtonPressed = true;
    }

    if (curCenter == LOW && lastCenterState == HIGH) {
      sendCommand("toggle=1");
      anyButtonPressed = true;
    }
    if (curLeft == LOW && lastLeftState == HIGH) {
      sendCommand("prev=1");
      anyButtonPressed = true;
    }
    if (curRight == LOW && lastRightState == HIGH) {
      sendCommand("next=1");
      anyButtonPressed = true;
    }

    // Resetuj timer aktywności przy każdym naciśnięciu przycisku
    if (anyButtonPressed) {
      lastActivityTime = millis();
    }

    lastCenterState = curCenter;
    lastLeftState = curLeft;
    lastRightState = curRight;
    lastUpState = curUp;
    lastDownState = curDown;
  }

  // Sprawdź bezczynność i przejdź w deep sleep
  // Nota: unsigned arithmetic poprawnie obsługuje przepełnienie millis()
  if (millis() - lastActivityTime > (DEEP_SLEEP_TIMEOUT_SEC * 1000)) {
    enterDeepSleep();
    // Kod poniżej nie zostanie wykonany
  }
}
