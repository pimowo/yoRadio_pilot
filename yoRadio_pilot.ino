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
#define FIRMWARE_VERSION "0.2"           // wersja oprogramowania
#define DEBUG_SERIAL true                // włącz/wyłącz komunikaty na UART (true = włączone, false = wyłączone)
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
// yoRadio
#define IP_YORADIO "192.168.1.101"       // IP yoRadio
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

// Makra dla komunikatów debug na UART
#if DEBUG_SERIAL
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebSocketsClient webSocket;

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

const unsigned char speakerIcon [] PROGMEM = {
  0b00011000, 0b00111000, 0b11111100, 0b11111100,
  0b11111100, 0b00111000, 0b00011000, 0b00001000
};

const unsigned char wifiErrorIcon[] PROGMEM = {
  0b00011000, 0b00100100, 0b01000010, 0b01000010,
  0b01000010, 0b00100100, 0b00011000, 0b00001000
};

// Enum stanów połączenia WiFi
enum WifiState { WIFI_CONNECTING, WIFI_ERROR, WIFI_OK };
WifiState wifiState = WIFI_CONNECTING;

unsigned long wifiTimer = 0;
const unsigned long wifiTimeout = 8000;

// Funkcja sprawdzająca czy player jest zatrzymany
// Zwraca true jeśli radio jest w stanie stop/pause/stopped/paused
bool isPlayerStopped() {
  return (!playerwrap.isEmpty() && 
          (playerwrap == "stop" || 
           playerwrap == "pause" ||
           playerwrap == "stopped" ||
           playerwrap == "paused"));
}

// ===== KONFIGURACJA SCROLLOWANIA =====
// Struktura konfiguracji scrollowania dla każdej linii tekstu na wyświetlaczu
struct ScrollConfig {
  int left;                    // pozycja X początkowa
  int top;                     // pozycja Y początkowa
  int fontsize;                // rozmiar czcionki (1, 2, 3...)
  int width;                   // szerokość dostępna dla tekstu
  unsigned long scrolldelay;   // opóźnienie między krokami scrollowania (ms)
  int scrolldelta;             // ilość pikseli przesunięcia na krok
  unsigned long scrolltime;    // czas pauzy przed rozpoczęciem scrollowania (ms)
};

// Konfiguracje dla 3 linii: stacja (linia 0), wykonawca/utwór (linia 1), utwór (linia 2)
const ScrollConfig scrollConfs[3] = {
  {2, 1, 2, SCREEN_WIDTH - 4, 10, 2, 1500},    // Linia 0: stacja
  {0, 19, 2, SCREEN_WIDTH - 2, 10, 2, 1500},   // Linia 1: wykonawca lub utwór
  {0, 38, 1, SCREEN_WIDTH - 2, 10, 2, 1500}    // Linia 2: utwór (gdy jest wykonawca)
};

// Struktura stanu scrollowania dla każdej linii
struct ScrollState {
  int pos;                     // aktualna pozycja scrollowania (piksele)
  unsigned long t_last;        // czas ostatniego kroku scrollowania
  unsigned long t_start;       // czas rozpoczęcia scrollowania
  bool scrolling;              // czy tekst wymaga scrollowania (jest za długi)
  bool isMoving;               // czy tekst obecnie się przesuwa
  String text;                 // tekst do wyświetlenia (z sufiksem jeśli scrolluje)
  int singleTextWidth;         // szerokość pojedynczego tekstu w pikselach
  int suffixWidth;             // szerokość sufiksu " * " w pikselach
};

// Tablica stanów scrollowania dla 3 linii
ScrollState scrollStates[3] = {0};

// Poprzednie wartości tekstów - do wykrywania zmian
String prev_stacja = "";
String prev_wykonawca = "";
String prev_utwor = "";

// Sufiks dodawany między powtórzeniami tekstu podczas scrollowania
const char* scrollSuffix = " * ";

// ===== SEKWENCYJNE PRZEWIJANIE =====
// Indeks aktualnie scrollowanej linii (0-2). Tylko jedna linia scrolluje w danym momencie
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

// ===== FUNKCJE SCROLLOWANIA =====

// Przygotowanie tekstu do scrollowania dla danej linii
// Sprawdza czy tekst mieści się w dostępnej przestrzeni i jeśli nie - dodaje sufiks oraz duplikat tekstu
void prepareScroll(int line, const String& txt, int scale) {
  int singleWidth = getPixelWidth5x7(txt, scale);
  int availWidth = scrollConfs[line].width;
  
  // Sprawdź TYLKO szerokość samego tekstu (bez sufiksu)
  bool needsScroll = singleWidth > availWidth;
  
  if (needsScroll) {
    // Tekst jest za długi - dodaj sufiks i duplikat tekstu dla płynnego scrollowania
    int suffixWidth = getPixelWidth5x7(String(scrollSuffix), scale);
    scrollStates[line].text = txt + String(scrollSuffix) + txt;  // "Tekst * Tekst"
    scrollStates[line].singleTextWidth = singleWidth;
    scrollStates[line].suffixWidth = suffixWidth;
    scrollStates[line].scrolling = true;
  } else {
    // Tekst się mieści - nie scrolluj, wyświetl bez sufiksu
    scrollStates[line].text = txt;
    scrollStates[line].singleTextWidth = singleWidth;
    scrollStates[line].suffixWidth = 0;  // Brak suffiksa
    scrollStates[line].scrolling = false;
  }
}

// Aktualizacja pozycji scrollowania dla danej linii
// Implementuje sekwencyjne scrollowanie - tylko jedna linia przewija się w danym momencie
void updateScroll(int line) {
  unsigned long now = millis();
  auto& conf = scrollConfs[line];
  auto& state = scrollStates[line];
  
  // Jeśli tekst nie wymaga scrollowania ale jest aktywną linią - odczekaj i przełącz na następną
  if (!state.scrolling) {
    if (line == activeScrollLine) {
      unsigned long elapsed = now - state.t_start;
      if (elapsed >= conf.scrolltime) {
        // Przełącz na następną linię (0->1->2->0...)
        activeScrollLine = (activeScrollLine + 1) % 3;
        scrollStates[activeScrollLine].t_start = now;
        scrollStates[activeScrollLine].t_last = now;
        scrollStates[activeScrollLine].pos = 0;
        scrollStates[activeScrollLine].isMoving = false;
      }
    }
    return;
  }
  
  // Aktualizuj tylko aktywną linię scrollowania
  if (line != activeScrollLine) return;
  
  unsigned long elapsed = now - state.t_start;
  
  // Jeśli tekst jest na początku i nie ruszył się jeszcze - odczekaj czas pauzy
  if (state.pos == 0 && ! state.isMoving) {
    if (elapsed >= conf.scrolltime) {
      state.isMoving = true;  // Rozpocznij scrollowanie
      state.t_last = now;
    }
    return;
  }
  
  // Przesuń tekst w lewo co scrolldelay milisekund
  if (now - state.t_last >= conf.scrolldelay) {
    state.pos -= conf.scrolldelta;  // Przesuń w lewo
    
    // Sprawdź czy osiągnięto pozycję resetu (koniec pierwszego tekstu + sufiks)
    int resetPos = -(state.singleTextWidth + state.suffixWidth);
    if (state.pos <= resetPos) {
      // Zresetuj pozycję - dzięki duplikatowi tekstu wygląda to na ciągłe scrollowanie
      state.pos = 0;
      state.isMoving = false;
      state.t_start = now;
      
      // Przełącz na następną linię
      activeScrollLine = (activeScrollLine + 1) % 3;
      scrollStates[activeScrollLine].t_start = now;
      scrollStates[activeScrollLine].t_last = now;
      scrollStates[activeScrollLine].pos = 0;
      scrollStates[activeScrollLine].isMoving = false;
    }
    
    state.t_last = now;
  }
}

// Rysowanie linii tekstu (scrollowanego lub statycznego)
// Linia 0 (stacja) jest w trybie negatywowym (czarny tekst na białym tle)
void drawScrollLine(int line, int scale) {
  auto& conf = scrollConfs[line];
  auto& state = scrollStates[line];
  
  int x = conf.left + state.pos;  // Pozycja X z uwzględnieniem przesunięcia scrollowania
  int y = conf.top;
  
  if (line == 0) {
    // Linia 0 (stacja) - tryb negatywowy
    drawString5x7(x, y, state.text, scale, SSD1306_BLACK);
  } else {
    // Linie 1 i 2 (wykonawca/utwór) - normalny tryb
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

// Obsługa zdarzeń WebSocket
// Połączenie z yoRadio przez WebSocket pozwala odbierać dane o stacji, utworze, głośności itp.
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    DEBUG_PRINTLN("WebSocket connected!");
    wsConnected = true;
    lastWebSocketMessage = millis();
    lastActivityTime = millis();  // Resetuj timer aktywności przy połączeniu
    webSocket.sendTXT("getindex=1");
    return;
  }

  if (type == WStype_TEXT) {
    lastWebSocketMessage = millis();
    lastActivityTime = millis();  // Resetuj timer aktywności przy odbiorze wiadomości
    DEBUG_PRINT("WebSocket message: ");
    DEBUG_PRINTLN((char*)payload);
    
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) {
      DEBUG_PRINT("JSON parse error: ");
      DEBUG_PRINTLN(error.c_str());
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
          DEBUG_PRINT("DEBUG: playerwrap = '");
          DEBUG_PRINT(playerwrap);
          DEBUG_PRINTLN("'");
        }
        if (id == "rssi") rssi = obj["value"].as<int>();
      }
    }

    if (wifiState == WIFI_OK) updateDisplay();
  }
  
  if (type == WStype_DISCONNECTED) {
    DEBUG_PRINTLN("WebSocket disconnected!");
    wsConnected = false;
    lastActivityTime = millis();  // Resetuj timer aktywności przy rozłączeniu
  }
}

// Wysłanie komendy do yoRadio przez WebSocket
void sendCommand(const char* cmd) {
  if (webSocket.isConnected()) {
    webSocket.sendTXT(cmd);
    DEBUG_PRINT("Sent: ");
    DEBUG_PRINTLN(cmd);
  }
}

// Funkcja przechodzenia w tryb głębokiego uśpienia (deep sleep)
// Deep sleep wyłącza większość układów ESP32, oszczędzając energię baterii
// Urządzenie wybudza się po naciśnięciu przycisku CENTER (GPIO5)
void enterDeepSleep() {
  DEBUG_PRINTLN("Preparing deep sleep...");
  
  // Wyłącz wyświetlacz OLED
  display.clearDisplay();
  display.display();
  display.ssd1306_command(0xAE);  // Komenda wyłączenia wyświetlacza
  delay(100);

  // Porządne zamknięcie usług sieciowych przed uśpieniem
  webSocket.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  // Konfiguracja wybudzenia przez przycisk CENTER (GPIO5)
  // EXT0 - pojedynczy pin wybudzający, bardziej niezawodny niż EXT1
  DEBUG_PRINTLN("Configuring EXT0 wakeup on GPIO5 (LOW)...");
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_5, 0); // 0 = LOW wybudza (przycisk zwiera do GND)

  // RTC_PERIPH musi być włączony aby GPIO wybudzający działał
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  // Wyłącz pamięć RTC (nie przechowujemy danych między uśpieniami)
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);

  DEBUG_PRINTLN("Entering deep sleep in 50 ms...");
  Serial.flush();  // Zawsze używaj Serial.flush() przed uśpieniem (nie makro DEBUG)
  delay(50);

  esp_deep_sleep_start();  // Wejście w deep sleep - kod po tej linii nie zostanie wykonany
}

void oledSetContrast(uint8_t c) {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(c);
}

void setup() {
  Serial.begin(115200);

  // Sprawdź przyczynę wybudzenia z deep sleep (diagnostyka)
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  switch (wakeupReason) {
    case ESP_SLEEP_WAKEUP_EXT0: DEBUG_PRINTLN("Wakeup reason: EXT0 (single pin)"); break;
    case ESP_SLEEP_WAKEUP_EXT1: DEBUG_PRINTLN("Wakeup reason: EXT1 (mask)"); break;
    case ESP_SLEEP_WAKEUP_TIMER: DEBUG_PRINTLN("Wakeup reason: TIMER"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: DEBUG_PRINTLN("Wakeup reason: TOUCHPAD"); break;
    case ESP_SLEEP_WAKEUP_ULP: DEBUG_PRINTLN("Wakeup reason: ULP"); break;
    case ESP_SLEEP_WAKEUP_UNDEFINED: DEBUG_PRINTLN("Wakeup reason: UNDEFINED / normal boot"); break;
    default: DEBUG_PRINTF("Wakeup reason: %d\n", wakeupReason); break;
  }

  delay(100);
  DEBUG_PRINT("\n\nStarting YoRadio OLED Display v");
  DEBUG_PRINTLN(FIRMWARE_VERSION);

  // Inicjalizacja watchdog timer - zabezpieczenie przed zawieszeniem programu
  // true = panic on timeout (reboot ESP32 jeśli watchdog nie zostanie zresetowany w czasie)
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);  // Dodaj obecne zadanie do monitorowania
  DEBUG_PRINTLN("Watchdog timer initialized");

  // Konfiguracja przycisków z rezystorami podciągającymi
  // Przyciski zwierają piny do masy (GND) gdy są naciśnięte
  pinMode(BTN_UP, INPUT_PULLUP);      // Przycisk GÓRA - zwiększ głośność
  pinMode(BTN_RIGHT, INPUT_PULLUP);   // Przycisk PRAWO - następna stacja
  pinMode(BTN_CENTER, INPUT_PULLUP);  // Przycisk CENTER/OK - play/pause
  pinMode(BTN_LEFT, INPUT_PULLUP);    // Przycisk LEWO - poprzednia stacja
  pinMode(BTN_DOWN, INPUT_PULLUP);    // Przycisk DÓŁ - zmniejsz głośność

  // Wyłącz diodę LED WS2812 (nie jest używana, oszczędność energii)
  strip.begin();
  strip.clear();
  strip.show();

  // Inicjalizacja wyświetlacza OLED SSD1306 128x64
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 failed"));  // Krytyczny błąd - zawsze wyświetl
    for(;;);  // Zatrzymaj program jeśli wyświetlacz nie działa
  }

  // Ustaw jasność/kontrast wyświetlacza OLED (0-15 mapowane na 0-240)
  uint8_t brightness = constrain(OLED_BRIGHTNESS, 0, 15);
  oledSetContrast(brightness * 16);
  DEBUG_PRINT("OLED brightness set to: ");
  DEBUG_PRINTLN(brightness);

  display.clearDisplay();
  display.display();

  // ===== KONFIGURACJA STATYCZNEGO ADRESU IP =====
  // Używamy statycznego IP dla szybszego i bardziej niezawodnego połączenia
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

  WiFi.mode(WIFI_STA);  // Tryb stacji WiFi (klient)
  WiFi.config(staticIP, gateway, subnet, dns1, dns2);

  // ===== ROZPOCZĘCIE ŁĄCZENIA Z SIECIĄ WiFi =====
  DEBUG_PRINT("Connecting to WiFi: ");
  DEBUG_PRINTLN(WIFI_SSID);
  DEBUG_PRINT("Using static IP: ");
  DEBUG_PRINTLN(STATIC_IP);
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiTimer = millis();
  wifiState = WIFI_CONNECTING;
  
  // ===== INICJALIZACJA OTA (Over-The-Air updates) =====
  // OTA pozwala na aktualizację firmware przez WiFi bez podłączania kabla USB
  ArduinoOTA.setHostname(OTAhostname);
  ArduinoOTA.setPassword(OTApassword);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    DEBUG_PRINTLN("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    DEBUG_PRINTLN("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    DEBUG_PRINTF("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    DEBUG_PRINTF("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      DEBUG_PRINTLN("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      DEBUG_PRINTLN("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      DEBUG_PRINTLN("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      DEBUG_PRINTLN("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      DEBUG_PRINTLN("End Failed");
    }
  });
  ArduinoOTA.begin();
  DEBUG_PRINTLN("OTA ready");
  
  // Inicjalizacja timerów aktywności i komunikacji
  lastActivityTime = millis();
  lastWebSocketMessage = millis();
  lastDisplayUpdate = millis();
}

void loop() {
  // Resetuj watchdog co iterację pętli (zapobiega rebootowi przez watchdog)
  esp_task_wdt_reset();
  
  // Obsługa aktualizacji OTA
  ArduinoOTA.handle();
  
  // Obsługa zdarzeń WebSocket
  webSocket.loop();

  // ===== MASZYNA STANÓW WiFi =====
  if (wifiState == WIFI_CONNECTING) {
    // Stan: Łączenie z WiFi
    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTLN("WiFi connected!");
      DEBUG_PRINT("IP: ");
      DEBUG_PRINTLN(WiFi.localIP());
      wifiState = WIFI_OK;
      // Rozpocznij łączenie z WebSocket po udanym połączeniu WiFi
      DEBUG_PRINT("Connecting to WebSocket at ");
      DEBUG_PRINT(IP_YORADIO);
      DEBUG_PRINTLN(":80/ws");
      webSocket.begin(IP_YORADIO, 80, "/ws");
      webSocket.onEvent(webSocketEvent);
    }
    if (millis() - wifiTimer > wifiTimeout) {
      DEBUG_PRINTLN("WiFi connection timeout!");
      wifiState = WIFI_ERROR;
    }
  } else if (wifiState == WIFI_OK) {
    // Stan: WiFi połączony - monitoruj czy połączenie jest aktywne
    if (WiFi.status() != WL_CONNECTED) {
      DEBUG_PRINTLN("WiFi disconnected!");
      wifiState = WIFI_CONNECTING;
      wifiTimer = millis();
    }
  } else if (wifiState == WIFI_ERROR) {
    // Stan: Błąd WiFi - próbuj ponownie połączyć
    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTLN("WiFi reconnected!");
      wifiState = WIFI_OK;
    }
  }

  // Aktualizuj wyświetlacz (z throttlingiem wbudowanym w funkcję)
  updateDisplay();

  // ===== OBSŁUGA PRZYCISKÓW =====
  // Sprawdzaj przyciski co ~120ms aby uniknąć zbyt częstego odpytywania
  if (millis() - lastButtonCheck > 120) {
    lastButtonCheck = millis();

    // Odczytaj aktualny stan wszystkich przycisków (LOW = naciśnięty)
    bool curUp = digitalRead(BTN_UP);
    bool curRight = digitalRead(BTN_RIGHT);
    bool curCenter = digitalRead(BTN_CENTER);
    bool curLeft = digitalRead(BTN_LEFT);
    bool curDown = digitalRead(BTN_DOWN);

    bool anyButtonPressed = false;

    // Przycisk GÓRA - zwiększ głośność (powtarzalne przy przytrzymaniu)
    if (curUp == LOW) {
      sendCommand("volp=1");
      volumeChanging = true;
      volumeChangeTime = millis();
      anyButtonPressed = true;
    }
    // Przycisk DÓŁ - zmniejsz głośność (powtarzalne przy przytrzymaniu)
    if (curDown == LOW) {
      sendCommand("volm=1");
      volumeChanging = true;
      volumeChangeTime = millis();
      anyButtonPressed = true;
    }

    // Przycisk CENTER - play/pause (wykrywanie zbocza: LOW po poprzednim HIGH)
    if (curCenter == LOW && lastCenterState == HIGH) {
      sendCommand("toggle=1");
      anyButtonPressed = true;
    }
    // Przycisk LEWO - poprzednia stacja (wykrywanie zbocza)
    if (curLeft == LOW && lastLeftState == HIGH) {
      sendCommand("prev=1");
      anyButtonPressed = true;
    }
    // Przycisk PRAWO - następna stacja (wykrywanie zbocza)
    if (curRight == LOW && lastRightState == HIGH) {
      sendCommand("next=1");
      anyButtonPressed = true;
    }

    // Resetuj timer aktywności przy każdym naciśnięciu przycisku
    // Zapobiega to przejściu w deep sleep podczas aktywnego użytkowania
    if (anyButtonPressed) {
      lastActivityTime = millis();
    }

    // Zapisz aktualny stan przycisków dla wykrywania zbocza w następnej iteracji
    lastCenterState = curCenter;
    lastLeftState = curLeft;
    lastRightState = curRight;
    lastUpState = curUp;
    lastDownState = curDown;
  }

  // ===== LOGIKA DEEP SLEEP =====
  // Sprawdź czas bezczynności i przejdź w deep sleep po odpowiednim czasie
  // Uwaga: arytmetyka unsigned long poprawnie obsługuje przepełnienie millis()
  unsigned long inactivityTime = millis() - lastActivityTime;
  
  // Sprawdź status playera i wybierz odpowiedni timeout
  // Gdy player jest zatrzymany - krótszy timeout (oszczędzanie baterii)
  // Gdy player gra - dłuższy timeout (użytkownik słucha)
  bool playerStopped = isPlayerStopped();
  unsigned long timeoutMs = playerStopped ? (DEEP_SLEEP_TIMEOUT_STOPPED_SEC * 1000) : (DEEP_SLEEP_TIMEOUT_SEC * 1000);
  
  // Przejdź w deep sleep TYLKO gdy:
  // 1. Upłynął czas bezczynności
  // 2. WiFi jest połączony (WIFI_OK)
  // 3. WebSocket jest połączony
  // To zapobiega przejściu w deep sleep podczas łączenia lub błędów połączenia
  if (inactivityTime > timeoutMs && 
      wifiState == WIFI_OK && 
      wsConnected) {
    if (playerStopped) {
      DEBUG_PRINTLN("Deep sleep triggered: Player stopped/paused timeout");
    } else {
      DEBUG_PRINTLN("Deep sleep triggered: General inactivity timeout");
    }
    enterDeepSleep();
    // Kod poniżej nie zostanie wykonany - urządzenie przeszło w deep sleep
  }
}
