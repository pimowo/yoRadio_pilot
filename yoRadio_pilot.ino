#include <WiFi.h>
#include <WebSocketsClient.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include "font5x7.h"

//==================================================================================================
// firmware
#define FIRMWARE_VERSION "0.3"           // wersja oprogramowania
// sieć
#define WIFI_SSID "pimowo"               // sieć 
#define WIFI_PASS "ckH59LRZQzCDQFiUgj"   // hasło sieci
#define STATIC_IP "192.168.1.111"        // IP
#define GATEWAY_IP "192.168.1.1"         // brama
#define SUBNET_MASK "255.255.255.0"      // maska
#define DNS1_IP "192.168.1.1"            // DNS 1
#define DNS2_IP "8.8.8.8"                // DNS 2
// OTA
#define OTAhostname "yoRadio_pilot"      // nazwa dla OTA
#define OTApassword "12345987"           // hasło dla OTA
// yoRadio - Multi-Radio Support
const char* RADIO_IPS[] = {
  "192.168.1.101",
  "192.168.1.102",
  "192.168.1.103"
};
const int NUM_RADIOS = sizeof(RADIO_IPS) / sizeof(RADIO_IPS[0]);  // Auto-calculated from array size
// uśpienie
#define DEEP_SLEEP_TIMEOUT_SEC 60        // sekundy bezczynności przed deep sleep (podczas odtwarzania)
#define DEEP_SLEEP_TIMEOUT_STOPPED_SEC 5 // sekundy bezczynności przed deep sleep (gdy zatrzymany)
// klawiattura
#define BTN_UP     7                     // pin GÓRA
#define BTN_RIGHT  4                     // pin PRAWO
#define BTN_CENTER 5                     // pin OK
#define BTN_LEFT   6                     // pin LEWO 
#define BTN_DOWN   3                     // pin DÓŁ
// wyświetlacz
#define OLED_BRIGHTNESS 10               // 0-15 (wartość * 16 daje zakres 0-240 dla kontrastu SSD1306)
#define DISPLAY_REFRESH_RATE_MS 50       // odświeżanie ekranu (100ms = 10 FPS)
// bateria
#define BATTERY_LOW_BLINK_MS 500         // interwał mrugania słabej baterii
// watchdog
#define WDT_TIMEOUT 30                   // timeout watchdog w sekundach
//==================================================================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebSocketsClient webSocket;

// RTC memory to persist radio selection through deep sleep
RTC_DATA_ATTR int currentRadio = 0;  // 0-based index (0 = radio #1)

#define LED_PIN 48       // GPIO 48
#define NUM_LEDS 1       // Ile LED-ów?  (1 jeśli jeden chipik)
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

int batteryPercent = 100;
String meta = "";
int volume = 0;
int bitrate = 0;
String fmt = "";
int rssi = 0;
String playerwrap = "";
String stacja = "";
String wykonawca = "";
String utwor = "";
bool showCreatorLine = true;

bool wsConnected = false;
unsigned long lastWebSocketMessage = 0;
const unsigned long WS_TIMEOUT_MS = 10000;  // 10 sekund timeout

unsigned long lastButtonCheck = 0;
unsigned long lastActivityTime = 0;
unsigned long lastDisplayUpdate = 0;
bool lastCenterState = HIGH;
bool lastLeftState = HIGH;
bool lastRightState = HIGH;
bool lastUpState = HIGH;
bool lastDownState = HIGH;

bool volumeChanging = false;
unsigned long volumeChangeTime = 0;
const unsigned long VOLUME_DISPLAY_TIME = 2000;  // 2 sekundy wyświetlania

// Long-press state for radio switching
unsigned long leftPressStartTime = 0;
unsigned long rightPressStartTime = 0;
bool leftActionExecuted = false;
bool rightActionExecuted = false;
bool leftPressReleased = true;
bool rightPressReleased = true;
const unsigned long LONG_PRESS_TIME = 2000;  // 2 seconds for long press

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

// Calculate width for line 1 (artist/track) - reduce by radio number width if NUM_RADIOS > 1
// Radio number format: " x " where x is 1-9 (e.g., " 9 ")
// Calculation: 3 chars * (5 pixels + 1 spacing) * scale 1 = 18 pixels
// Note: NUM_RADIOS is validated at startup to not exceed 9 for single-digit display
const int RADIO_NUMBER_WIDTH = (NUM_RADIOS > 1) ? 18 : 0;

const ScrollConfig scrollConfs[3] = {
  {2, 1, 2, SCREEN_WIDTH - 4, 10, 2, 1500},
  {0, 19, 2, SCREEN_WIDTH - 2 - RADIO_NUMBER_WIDTH, 10, 2, 1500},
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
  for (size_t i = 0; i < s.length();) {
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
      state.pos = 0;
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
  // Throttle display updates to DISPLAY_REFRESH_RATE_MS
  if (millis() - lastDisplayUpdate < DISPLAY_REFRESH_RATE_MS) {
    return;
  }
  lastDisplayUpdate = millis();
  
  display.clearDisplay();

  // Check if volume screen should be hidden
  if (volumeChanging && (millis() - volumeChangeTime > VOLUME_DISPLAY_TIME)) {
    volumeChanging = false;
  }

  // VOLUME SCREEN
  if (volumeChanging) {
    // Top bar with "GŁOŚNOŚĆ" in negative mode
    display.fillRect(0, 0, SCREEN_WIDTH, 16, SSD1306_WHITE);
    String headerText = "GŁOŚNOŚĆ";
    int headerWidth = getPixelWidth5x7(headerText, 2);  // ← ZMIEŃ NA 2
    int headerX = (SCREEN_WIDTH - headerWidth) / 2;
    drawString5x7(headerX, 1, headerText, 2, SSD1306_BLACK);  // ← ZMIEŃ fontsize NA 2
    
    // Center: Volume number with scale 2 (IDENTYCZNIE JAK STACJA)
    int volScale = 3;  // ← ZMIEŃ Z 3 NA 2
    String volText = String(volume);
    int volTextWidth = getPixelWidth5x7(volText, volScale);
    int totalWidth = volTextWidth;
    int startX = (SCREEN_WIDTH - totalWidth) / 2;
    int centerY = 25;  // ← ZMIEŃ Z 25 NA 19 (identycznie jak artysta)
    
    // Draw volume number (BEZ IKONY)
    drawString5x7(startX, centerY, volText, volScale, SSD1306_WHITE);
    
    // Bottom: IP address
    String ipText = "IP:" + WiFi.localIP().toString();
    int ipY = 54;
    drawString5x7(2, ipY, ipText, 1, SSD1306_WHITE);
    
    display.display();
    return;
  }

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
    
    // Wyświetl wersję firmware na dole
    String versionText = "v" + String(FIRMWARE_VERSION);
    int versionWidth = getPixelWidth5x7(versionText, 1);
    int versionX = (SCREEN_WIDTH - versionWidth) / 2;
    int versionY = 52;
    drawString5x7(versionX, versionY, versionText, 1, SSD1306_WHITE);
    
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
    scrollStates[0]. pos = 0;
    scrollStates[0].t_start = millis();
    scrollStates[0].t_last = millis();
    scrollStates[0].isMoving = false;
    prepareScroll(0, stacja, scrollConfs[0]. fontsize);
  }

  // Jeśli artysty brak, utwór przeskakuje do drugiej linii
  if (wykonawca. isEmpty()) {
    // Artysty brak - utwór na linii artysty
    if (utwor != prev_utwor) {
      prev_utwor = utwor;
      scrollStates[1].pos = 0;
      scrollStates[1].t_start = millis();
      scrollStates[1].t_last = millis();
      scrollStates[1].isMoving = false;
      prepareScroll(1, utwor, scrollConfs[1]. fontsize);
    }
    showCreatorLine = false;
  } else {
    // Jest artysta - normalne wyświetlanie
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
    showCreatorLine = true;
  }

  // === UPDATE SCROLLS ===
  updateScroll(0);
  if (showCreatorLine) {
    updateScroll(1);
    updateScroll(2);
  } else {
    updateScroll(1);  // Tylko linia z utworem
  }

  // === DRAW ALL LINES ===
  drawScrollLine(0, scrollConfs[0].fontsize);
  if (showCreatorLine) {
    drawScrollLine(1, scrollConfs[1].fontsize);  // Artysta
    drawScrollLine(2, scrollConfs[2]. fontsize);  // Utwór
  } else {
    drawScrollLine(1, scrollConfs[1]. fontsize);  // Utwór na linii artysty
  }
  
  // === DRAW RADIO NUMBER (if NUM_RADIOS > 1) ===
  if (NUM_RADIOS > 1) {
    // Display radio number in top-right of track line (line 2)
    // Format: " x " (with spaces) where x = radio number (1-9)
    // Style: font size 1, negative mode (white background, black text)
    String radioText = " " + String(currentRadio + 1) + " ";
    int radioWidth = getPixelWidth5x7(radioText, 1);
    int radioX = SCREEN_WIDTH - radioWidth;
    int radioY = scrollConfs[1].top;  // Use scroll config for consistency
    
    // Draw white background
    display.fillRect(radioX, radioY, radioWidth, 8, SSD1306_WHITE);
    // Draw black text on white background
    drawString5x7(radioX, radioY, radioText, 1, SSD1306_BLACK);
  }

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
  
  // Animacja mrugania dla słabej baterii (< 20%)
  bool showBattery = true;
  if (batteryPercent < 20) {
    showBattery = ((millis() / BATTERY_LOW_BLINK_MS) % 2) == 0;
  }
  
  if (showBattery) {
    display.drawRect(batX, yLine, batWidth, batHeight, SSD1306_WHITE);
    display.fillRect(batX + batWidth, yLine + 2, 2, batHeight - 4, SSD1306_WHITE);
    int fillWidth = (batteryPercent * (batWidth - 2)) / 100;
    if (fillWidth > 0) display.fillRect(batX + 1, yLine + 1, fillWidth, batHeight - 2, SSD1306_WHITE);
  }

  int volX = 52;
  display.drawBitmap(volX, yLine, speakerIcon, 8, 8, SSD1306_WHITE);
  display.setCursor(volX + 10, yLine);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.print(volume);

  // Wyświetl bitrate TYLKO gdy radio gra (playerwrap != "stop" i != "pause")
  bool isPlaying = (!playerwrap.isEmpty() && 
                    playerwrap != "stop" && 
                    playerwrap != "pause" &&
                    playerwrap != "stopped" &&
                    playerwrap != "paused");

  if (isPlaying) {
    display.setCursor(85, yLine);
    if (bitrate > 0) {
      display.print(bitrate);
    }
    if (!fmt.isEmpty()) {
      display.print(fmt);
    }
  }

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
      JsonArray arr = doc["payload"].as<JsonArray>();
      for (JsonObject obj : arr) {
        String id = obj["id"].as<String>();
        if (id == "nameset") stacja = obj["value"].as<String>();
        if (id == "meta") {
            String metaStr = obj["value"]. as<String>();
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
          Serial.print("DEBUG: playerwrap = '");  // ← DODAJ
          Serial.print(playerwrap);              // ← DODAJ
          Serial.println("'");
        }
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

void switchToRadio(int newRadioIndex) {
  if (newRadioIndex < 0 || newRadioIndex >= NUM_RADIOS) {
    Serial.println("Radio index out of bounds, ignoring");
    return;
  }
  
  if (newRadioIndex == currentRadio) {
    Serial.println("Already on this radio, ignoring");
    return;
  }
  
  Serial.print("Switching from radio #");
  Serial.print(currentRadio + 1);
  Serial.print(" to radio #");
  Serial.println(newRadioIndex + 1);
  
  // Disconnect from old radio
  webSocket.disconnect();
  wsConnected = false;
  
  // Update current radio
  currentRadio = newRadioIndex;
  
  // Connect to new radio
  Serial.print("Connecting to new radio at ");
  Serial.print(RADIO_IPS[currentRadio]);
  Serial.println(":80/ws");
  
  webSocket.begin(RADIO_IPS[currentRadio], 80, "/ws");
  webSocket.onEvent(webSocketEvent);
  lastWebSocketMessage = millis();
  
  // Clear display state
  stacja = "";
  wykonawca = "";
  utwor = "";
  prev_stacja = "";
  prev_wykonawca = "";
  prev_utwor = "";
}

void enterDeepSleep() {
  Serial.println("Preparing deep sleep...");
  display.clearDisplay();
  display.display();
  display.ssd1306_command(0xAE);  // Wyłącz OLED
  delay(100);

  // Porządne zamknięcie usług sieciowych
  webSocket.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  // Upewnij się, że przycisk CENTER (GPIO5) jest INPUT_PULLUP - już masz w setup()
  // Używamy EXT0: pojedynczy pin wybudzający (pewniejszy)
  Serial.println("Configuring EXT0 wakeup on GPIO5 (LOW)...");
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_5, 0); // 0 = LOW wakes up (przycisk zwiera do GND)

  // RTC_PERIPH MUSI BYĆ WŁĄCZONY, żeby RTC IO (EXT0/EXT1) działało.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  // Keep RTC slow memory ON to preserve currentRadio variable
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);

  Serial.println("Entering deep sleep in 50 ms...");
  Serial.flush();
  delay(50);

  esp_deep_sleep_start();
}

void oledSetContrast(uint8_t c) {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(c);
}

void setup() {
  Serial.begin(115200);

  // --- DEBUG: pokaz przyczynę wake ---
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  switch (wakeupReason) {
    case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Wakeup reason: EXT0 (single pin)"); break;
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Wakeup reason: EXT1 (mask)"); break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Wakeup reason: TIMER"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Wakeup reason: TOUCHPAD"); break;
    case ESP_SLEEP_WAKEUP_ULP: Serial.println("Wakeup reason: ULP"); break;
    case ESP_SLEEP_WAKEUP_UNDEFINED: Serial.println("Wakeup reason: UNDEFINED / normal boot"); break;
    default: Serial.printf("Wakeup reason: %d\n", wakeupReason); break;
  }

  delay(100);
  Serial.print("\n\nStarting YoRadio OLED Display v");
  Serial.println(FIRMWARE_VERSION);
  
  // Validate NUM_RADIOS limit
  if (NUM_RADIOS > 9) {
    Serial.println("ERROR: NUM_RADIOS exceeds maximum of 9!");
    Serial.println("Please reduce the number of radios in RADIO_IPS array.");
    for(;;);  // Halt execution
  }
  
  // Validate and restore current radio from RTC memory
  if (currentRadio < 0 || currentRadio >= NUM_RADIOS) {
    Serial.println("Invalid radio index in RTC memory, resetting to 0");
    currentRadio = 0;
  }
  Serial.print("Current radio: #");
  Serial.print(currentRadio + 1);
  Serial.print(" (");
  Serial.print(RADIO_IPS[currentRadio]);
  Serial.println(")");

  // Inicjalizacja watchdog timer
  // true = panic on timeout (reboot ESP32 jeśli watchdog nie zostanie zresetowany)
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);  // Dodaj current task
  Serial.println("Watchdog timer initialized");

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  // === INICJALIZUJ I WYŁĄCZ WS2812 ===
  // strip.begin();
  // strip. show();          // Włącz komunikację
  // strip.setBrightness(0); // Ustaw jasność na 0 (wyłączone)
  // strip.clear();          // Wyczyść wszystkie LED
  // strip.show();           // Wyślij do LED

  // === WYŁĄCZ WS2812 LED ===
  strip.begin();
  strip.clear();
  strip.show();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 failed"));
    for(;;);
  }

  // Ustaw jasność OLED (0-15 mapuje na 0-255)
  uint8_t brightness = constrain(OLED_BRIGHTNESS, 0, 15);
  //display.setContrast(brightness * 16);
  oledSetContrast(brightness * 16);
  Serial.print("OLED brightness set to: ");
  Serial.println(brightness);

  display.clearDisplay();
  display.display();

  // ===== STATYCZNE IP =====
  IPAddress staticIP;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns1;
  IPAddress dns2;

  staticIP.fromString(STATIC_IP);
  gateway.fromString(GATEWAY_IP);
  subnet.fromString(SUBNET_MASK);
  dns1.fromString(DNS1_IP);
  dns2.fromString(DNS2_IP);

  WiFi.mode(WIFI_STA);
  WiFi.config(staticIP, gateway, subnet, dns1, dns2);

  // ===== WIFI BEGIN =====
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  Serial.print("Using static IP: ");
  Serial.println(STATIC_IP);
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiTimer = millis();
  wifiState = WIFI_CONNECTING;
  
  // Inicjalizacja ArduinoOTA
  ArduinoOTA.setHostname(OTAhostname);
  ArduinoOTA.setPassword(OTApassword);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("OTA ready");
  
  // Inicjalizacja timerów
  lastActivityTime = millis();
  lastWebSocketMessage = millis();
  lastDisplayUpdate = millis();
}

void loop() {
  // Reset watchdog co iterację
  esp_task_wdt_reset();
  
  // Obsługa OTA updates
  ArduinoOTA.handle();
  
  webSocket.loop();

  if (wifiState == WIFI_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      wifiState = WIFI_OK;
      Serial.print("Connecting to WebSocket at ");
      Serial.print(RADIO_IPS[currentRadio]);
      Serial.println(":80/ws");
      webSocket.begin(RADIO_IPS[currentRadio], 80, "/ws");
      webSocket.onEvent(webSocketEvent);
      lastWebSocketMessage = millis();
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
      volumeChanging = true;
      volumeChangeTime = millis();
      anyButtonPressed = true;
    }
    if (curDown == LOW) {
      sendCommand("volm=1");
      volumeChanging = true;
      volumeChangeTime = millis();
      anyButtonPressed = true;
    }

    if (curCenter == LOW && lastCenterState == HIGH) {
      sendCommand("toggle=1");
      anyButtonPressed = true;
    }
    
    // LEFT button: short press = prev station, long press (2s) = switch to previous radio
    if (curLeft == LOW && lastLeftState == HIGH) {
      // Button just pressed
      leftPressStartTime = millis();
      leftActionExecuted = false;
      leftPressReleased = false;
    }
    if (curLeft == LOW && !leftActionExecuted && !leftPressReleased) {
      // Button is being held
      if (millis() - leftPressStartTime >= LONG_PRESS_TIME) {
        // Long press action - switch to previous radio
        if (NUM_RADIOS > 1) {
          switchToRadio(currentRadio - 1);
          anyButtonPressed = true;
        }
        leftActionExecuted = true;  // Mark action as executed
      }
    }
    if (curLeft == HIGH && lastLeftState == LOW) {
      // Button released
      if (!leftActionExecuted) {
        // Short press - send prev command
        sendCommand("prev=1");
        anyButtonPressed = true;
      }
      leftPressReleased = true;  // Allow next press cycle
    }
    
    // RIGHT button: short press = next station, long press (2s) = switch to next radio
    if (curRight == LOW && lastRightState == HIGH) {
      // Button just pressed
      rightPressStartTime = millis();
      rightActionExecuted = false;
      rightPressReleased = false;
    }
    if (curRight == LOW && !rightActionExecuted && !rightPressReleased) {
      // Button is being held
      if (millis() - rightPressStartTime >= LONG_PRESS_TIME) {
        // Long press action - switch to next radio
        if (NUM_RADIOS > 1) {
          switchToRadio(currentRadio + 1);
          anyButtonPressed = true;
        }
        rightActionExecuted = true;  // Mark action as executed
      }
    }
    if (curRight == HIGH && lastRightState == LOW) {
      // Button released
      if (!rightActionExecuted) {
        // Short press - send next command
        sendCommand("next=1");
        anyButtonPressed = true;
      }
      rightPressReleased = true;  // Allow next press cycle
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
  unsigned long inactivityTime = millis() - lastActivityTime;
  
  // Sprawdź status playera i wybierz odpowiedni timeout
  // Jeśli playerwrap nie został jeszcze zainicjowany (pusty), traktuj jako playing
  // bool playerStopped = (!playerwrap.isEmpty() && (playerwrap == "stop" || playerwrap == "pause"));
  bool playerStopped = (! playerwrap.isEmpty() && 
                       (playerwrap == "stop" || 
                       playerwrap == "pause" ||
                       playerwrap == "stopped" ||
                       playerwrap == "paused"));
  unsigned long timeoutMs = playerStopped ? (DEEP_SLEEP_TIMEOUT_STOPPED_SEC * 1000) : (DEEP_SLEEP_TIMEOUT_SEC * 1000);
  
  if (inactivityTime > timeoutMs) {
    if (playerStopped) {
      Serial.println("Deep sleep triggered: Player stopped/paused timeout");
    } else {
      Serial.println("Deep sleep triggered: General inactivity timeout");
    }
    enterDeepSleep();
    // Kod poniżej nie zostanie wykonany
  }
}
