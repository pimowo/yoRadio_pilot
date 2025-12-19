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

// ====================== USTAWIENIA / SETTINGS ======================
// Debug UART messages: ustaw na 1 aby włączyć diagnostykę po UART, 0 aby wyłączyć
#define DEBUG_UART 0

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
// yoRadio - multi-radio support
const char* RADIO_IPS[] = {
  "192.168.1.133",                       // Radio 1
  "192.168.1.104",                       // Radio 2
  "192.168.1.103"                        // Radio 3
};
#define NUM_RADIOS 1                     // liczba dostępnych radiów
// uśpienie
#define DEEP_SLEEP_TIMEOUT_SEC 60        // sekundy bezczynności przed deep sleep (podczas odtwarzania)
#define DEEP_SLEEP_TIMEOUT_STOPPED_SEC 5 // sekundy bezczynności przed deep sleep (gdy zatrzymany)
// klawiattura
#define BTN_UP     7                     // pin GÓRA
#define BTN_RIGHT  4                     // pin PRAWO
#define BTN_CENTER 5                     // pin OK
#define BTN_LEFT   6                     // pin LEWO 
#define BTN_DOWN   3                     // pin DÓŁ
#define LONG_PRESS_TIME 2000             // czas long-press w ms (2 sekundy)
// wyświetlacz
#define OLED_BRIGHTNESS 10               // 0-15 (wartość * 16 daje zakres 0-240 dla kontrastu SSD1306)
#define DISPLAY_REFRESH_RATE_MS 50       // odświeżanie ekranu (100ms = 10 FPS)
// bateria
#define BATTERY_LOW_BLINK_MS 500         // interwał mrugania słabej baterii
// watchdog
#define WDT_TIMEOUT 30                   // timeout watchdog w sekundach
// ==================================================================================================

// Debug helpers
#if DEBUG_UART
  #define DPRINT(x) Serial.print(x)
  #define DPRINTLN(x) Serial.println(x)
  #define DPRINTF(fmt, ...) Serial.printf((fmt), __VA_ARGS__)
#else
  #define DPRINT(x)
  #define DPRINTLN(x)
  #define DPRINTF(fmt, ...)
#endif

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebSocketsClient webSocket;

#define LED_PIN 48       // GPIO 48
#define NUM_LEDS 1       // Ile LED-ów?  (1 jeśli jeden chipik)
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Multi-radio support - current radio stored in RTC memory (persists through deep sleep)
RTC_DATA_ATTR int currentRadio = 0;

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

// Track previous showCreatorLine to detect transitions
bool prevShowCreatorLine = true;

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

// CENTER button long-press state for radio switching
unsigned long centerPressStartTime = 0;
bool centerActionExecuted = false;
bool centerPressReleased = true;

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

// NOTE: przywrócono zachowanie bez rezerwacji miejsca na numer radia w górnym pasku
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

ScrollState scrollStates[3];

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

// Improved prepareScroll: initializes scroll state (pos, timers, flags).
// Ensures consistent initialization so scrolling will start when the line becomes active.
void prepareScroll(int line, const String& txt, int scale) {
  int singleWidth = getPixelWidth5x7(txt, scale);
  int availWidth = scrollConfs[line].width;

  // Determine whether we need scrolling
  bool needsScroll = singleWidth > availWidth;

  scrollStates[line].singleTextWidth = singleWidth;
  scrollStates[line].pos = 0;
  scrollStates[line].t_start = millis();
  scrollStates[line].t_last = millis();
  scrollStates[line].isMoving = false;

  if (needsScroll) {
    int suffixWidth = getPixelWidth5x7(String(scrollSuffix), scale);
    scrollStates[line].text = txt + String(scrollSuffix) + txt;
    scrollStates[line].suffixWidth = suffixWidth;
    scrollStates[line].scrolling = true;
  } else {
    scrollStates[line].text = txt;
    scrollStates[line].suffixWidth = 0;
    scrollStates[line].scrolling = false;
  }
}

void updateScroll(int line) {
  unsigned long now = millis();
  auto& conf = scrollConfs[line];
  auto& state = scrollStates[line];

  // If not set to scroll, handle wait time on active line and then advance
  if (!state.scrolling) {
    if (line == activeScrollLine) {
      unsigned long elapsed = now - state.t_start;
      if (elapsed >= conf.scrolltime) {
        activeScrollLine = (activeScrollLine + 1) % 3;
        // initialize next active line timers to avoid immediate skip
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

  // Start moving after configured pause
  if (state.pos == 0 && !state.isMoving) {
    if (elapsed >= conf.scrolltime) {
      state.isMoving = true;
      state.t_last = now;
    }
    return;
  }

  if (now - state.t_last >= conf.scrolldelay) {
    state.pos -= conf.scrolldelta;

    // Reset position after we've scrolled past one copy + suffix so the repeated copy aligns
    int resetPos = -(state.singleTextWidth + state.suffixWidth);
    if (state.pos <= resetPos) {
      state.pos = 0;
      state.isMoving = false;
      state.t_start = now;

      // move to next active line
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

// Draw radio number OR RSSI at bottom-left (same vertical position as vol/bitrate).
// - If there's more than one RADIO_IP (NUM_RADIOS > 1) draw radio number in negative
//   (white background + black built-in font) starting at x=0, format " x ".
// - If only one radio IP configured, draw RSSI bars (original behavior).
void drawRadioOrRssiBottom() {
  const int yLine = 52;
  if (NUM_RADIOS > 1) {
    String radioText = " " + String(currentRadio + 1) + " ";
    // built-in font approximate width: 6 px per char at textSize=1
    int charWidth = 6;
    int textWidth = radioText.length() * charWidth;
    int boxX = 0;
    int boxY = yLine;
    int boxW = textWidth;
    int boxH = 8 + 2;

    // Fill background (negative) - no outline/frame, just background behind text
    display.fillRect(boxX, boxY, boxW, boxH, SSD1306_WHITE);

    // Draw black text on white background using built-in font (same as vol/bitrate)
    display.setTextSize(1);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(boxX, boxY + 1);
    display.print(radioText);

    // restore color for subsequent drawings
    display.setTextColor(SSD1306_WHITE);
  } else {
    // Single radio configured: show RSSI bars (same as original)
    int rssiX = 0;
    int barWidth = 3;
    int barSpacing = 2;
    int barHeights[4] = {2, 4, 6, 8};
    long rssiValue = WiFi.RSSI();
    int rssiPercent = constrain(map((int)rssiValue, -90, -30, 0, 100), 0, 100);
    int bars = map(rssiPercent, 0, 100, 0, 4);
    for (int i = 0; i < 4; i++) {
      int x = rssiX + i * (barWidth + barSpacing);
      if (i < bars)
        display.fillRect(x, yLine + (8 - barHeights[i]), barWidth, barHeights[i], SSD1306_WHITE);
      else
        display.drawFastHLine(x, yLine + (8 - barHeights[i]), barWidth, SSD1306_WHITE);
    }
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
    int headerWidth = getPixelWidth5x7(headerText, 2);
    int headerX = (SCREEN_WIDTH - headerWidth) / 2;
    drawString5x7(headerX, 1, headerText, 2, SSD1306_BLACK);

    // Center: Volume number
    int volScale = 3;
    String volText = String(volume);
    int volTextWidth = getPixelWidth5x7(volText, volScale);
    int totalWidth = volTextWidth;
    int startX = (SCREEN_WIDTH - totalWidth) / 2;
    int centerY = 25;

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

    // additionally show radio number or RSSI at bottom-left even on "Brak yoRadio" screen
    drawRadioOrRssiBottom();

    display.display();
    return;  // Wyjdź z funkcji, nie rysuj reszty
  }

  // MAIN SCREEN (WiFi OK)
  const int16_t lineHeight = 16;
  display.fillRect(0, 0, SCREEN_WIDTH, lineHeight, SSD1306_WHITE);

  // === PREPARE SCROLLS - TYLKO PRZY ZMIANIE =====
  // Save previous showCreatorLine to detect transitions (we need to reinitialize scrolls on toggle)
  bool oldShowCreatorLine = showCreatorLine;

  // Station line: always prepare when changed
  if (stacja != prev_stacja) {
    prev_stacja = stacja;
    prepareScroll(0, stacja, scrollConfs[0].fontsize);
  }

  // Jeśli artysty brak, utwór przeskakuje do drugiej linii
  if (wykonawca.isEmpty()) {
    // Artysty brak - utwór na linii artysty
    // prepare when utwor changed OR when we just toggled showCreatorLine state (so identical text moved to another line still gets initialized)
    if (utwor != prev_utwor || oldShowCreatorLine != false) {
      prev_utwor = utwor;
      prepareScroll(1, utwor, scrollConfs[1].fontsize);
    }
    showCreatorLine = false;
  } else {
    // Jest artysta - normalne wyświetlanie
    // If either wykonawca changed OR we just toggled showCreatorLine state (so we must reinit)
    if (wykonawca != prev_wykonawca || oldShowCreatorLine != true) {
      prev_wykonawca = wykonawca;
      prepareScroll(1, wykonawca, scrollConfs[1].fontsize);
    }

    if (utwor != prev_utwor || oldShowCreatorLine != true) {
      prev_utwor = utwor;
      prepareScroll(2, utwor, scrollConfs[2].fontsize);
    }
    showCreatorLine = true;
  }

  // If showCreatorLine toggled (3 lines <-> 2 lines), reset activeScrollLine and initialize timers
  if (oldShowCreatorLine != showCreatorLine) {
    activeScrollLine = 0;
    // initialize visible lines to avoid being stuck referencing invisible line
    unsigned long now = millis();
    // line 0 always visible
    scrollStates[0].t_start = now;
    scrollStates[0].t_last = now;
    scrollStates[0].pos = 0;
    scrollStates[0].isMoving = false;
    // line 1 visible in both modes
    scrollStates[1].t_start = now;
    scrollStates[1].t_last = now;
    scrollStates[1].pos = 0;
    scrollStates[1].isMoving = false;
    if (showCreatorLine) {
      // line 2 becomes visible; initialize it
      scrollStates[2].t_start = now;
      scrollStates[2].t_last = now;
      scrollStates[2].pos = 0;
      scrollStates[2].isMoving = false;
    }
  } else {
    // Ensure activeScrollLine is within visible range (0..1 when no creator line)
    if (!showCreatorLine && activeScrollLine > 1) {
      activeScrollLine = 0;
    }
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
    drawScrollLine(2, scrollConfs[2].fontsize);  // Utwór
  } else {
    drawScrollLine(1, scrollConfs[1].fontsize);  // Utwór na linii artysty
  }

  // === RADIO NUMBER / RSSI DISPLAY (bottom-left) ===
  drawRadioOrRssiBottom();

  // BATTERY (unchanged)
  const int yLine = 52;
  int batX = 25;
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

  // VOLUME ICON and NUMBER (unchanged)
  int volX = 54;
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
  DPRINTLN(")");

  // Disconnect from current WebSocket
  if (webSocket.isConnected()) {
    webSocket.disconnect();
    wsConnected = false;
  }

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

  // Connect to new radio
  DPRINT("Connecting to WebSocket at ");
  DPRINT(RADIO_IPS[currentRadio]);
  DPRINTLN(":80/ws");
  webSocket.begin(RADIO_IPS[currentRadio], 80, "/ws");
  webSocket.onEvent(webSocketEvent);
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

void enterDeepSleep() {
  DPRINTLN("Preparing deep sleep...");
  display.clearDisplay();
  display.display();
  display.ssd1306_command(0xAE);  // Wyłącz OLED
  delay(100);

  // Porządne zamknięcie usług sieciowych
  webSocket.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  DPRINTLN("Configuring EXT0 wakeup on GPIO5 (LOW)...");
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_5, 0); // 0 = LOW wakes up (przycisk zwiera do GND)

  // RTC_PERIPH MUSI BYĆ WŁĄCZONY, żeby RTC IO (EXT0/EXT1) działało.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  // Enable RTC slow memory to preserve currentRadio variable
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);

  DPRINTLN("Entering deep sleep in 50 ms...");
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

  // Initialize default scrollStates to safe defaults
  for (int i = 0; i < 3; ++i) {
    scrollStates[i].pos = 0;
    scrollStates[i].t_last = 0;
    scrollStates[i].t_start = 0;
    scrollStates[i].scrolling = false;
    scrollStates[i].isMoving = false;
    scrollStates[i].text = "";
    scrollStates[i].singleTextWidth = 0;
    scrollStates[i].suffixWidth = 0;
  }

  // --- DEBUG: pokaz przyczynę wake ---
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  switch (wakeupReason) {
    case ESP_SLEEP_WAKEUP_EXT0: DPRINTLN("Wakeup reason: EXT0 (single pin)"); break;
    case ESP_SLEEP_WAKEUP_EXT1: DPRINTLN("Wakeup reason: EXT1 (mask)"); break;
    case ESP_SLEEP_WAKEUP_TIMER: DPRINTLN("Wakeup reason: TIMER"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: DPRINTLN("Wakeup reason: TOUCHPAD"); break;
    case ESP_SLEEP_WAKEUP_ULP: DPRINTLN("Wakeup reason: ULP"); break;
    case ESP_SLEEP_WAKEUP_UNDEFINED: DPRINTLN("Wakeup reason: UNDEFINED / normal boot"); break;
    default: DPRINTF("Wakeup reason: %d\n", wakeupReason); break;
  }

  delay(100);
  DPRINT("\n\nStarting YoRadio OLED Display v");
  DPRINTLN(FIRMWARE_VERSION);

  // Validate currentRadio index from RTC memory
  if (currentRadio < 0 || currentRadio >= NUM_RADIOS) {
    DPRINT("Invalid currentRadio from RTC: ");
    DPRINTLN(currentRadio);
    currentRadio = 0;
  }
  DPRINT("Current radio: ");
  DPRINT(currentRadio + 1);
  DPRINT(" (");
  DPRINT(RADIO_IPS[currentRadio]);
  DPRINTLN(")");

  // Inicjalizacja watchdog timer
  // true = panic on timeout (reboot ESP32 jeśli watchdog nie zostanie zresetowany)
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);  // Dodaj current task
  DPRINTLN("Watchdog timer initialized");

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  // === WYŁĄCZ WS2812 LED ===
  strip.begin();
  strip.clear();
  strip.show();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    DPRINTLN(F("SSD1306 failed"));
    for(;;);
  }

  // Ustaw jasność OLED (0-15 mapuje na 0-255)
  uint8_t brightness = constrain(OLED_BRIGHTNESS, 0, 15);
  oledSetContrast(brightness * 16);
  DPRINT("OLED brightness set to: ");
  DPRINTLN(brightness);

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
  DPRINT("Connecting to WiFi: ");
  DPRINTLN(WIFI_SSID);
  DPRINT("Using static IP: ");
  DPRINTLN(STATIC_IP);

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
    DPRINT("Start updating ");
    DPRINTLN(type);
  });
  ArduinoOTA.onEnd([]() {
    DPRINTLN("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (total > 0) {
      unsigned int percent = (unsigned int)((uint32_t)progress * 100 / total);
      DPRINTF("Progress: %u%%\r", percent);
    } else {
      DPRINTF("Progress: %u/?\r", progress);
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    DPRINTF("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      DPRINTLN("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      DPRINTLN("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      DPRINTLN("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      DPRINTLN("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      DPRINTLN("End Failed");
    }
  });
  ArduinoOTA.begin();
  DPRINTLN("OTA ready");

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
      DPRINTLN("WiFi connected!");
      DPRINT("IP: ");
      DPRINTLN(WiFi.localIP());
      wifiState = WIFI_OK;
      DPRINT("Connecting to WebSocket at ");
      DPRINT(RADIO_IPS[currentRadio]);
      DPRINTLN(":80/ws");
      webSocket.begin(RADIO_IPS[currentRadio], 80, "/ws");
      webSocket.onEvent(webSocketEvent);
    }
    if (millis() - wifiTimer > wifiTimeout) {
      DPRINTLN("WiFi connection timeout!");
      wifiState = WIFI_ERROR;
    }
  } else if (wifiState == WIFI_OK) {
    if (WiFi.status() != WL_CONNECTED) {
      DPRINTLN("WiFi disconnected!");
      wifiState = WIFI_CONNECTING;
      wifiTimer = millis();
    }
  } else if (wifiState == WIFI_ERROR) {
    if (WiFi.status() == WL_CONNECTED) {
      DPRINTLN("WiFi reconnected!");
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

    // UP button - volume up
    if (curUp == LOW) {
      sendCommand("volp=1");
      volumeChanging = true;
      volumeChangeTime = millis();
      anyButtonPressed = true;
    }

    // DOWN button - volume down
    if (curDown == LOW) {
      sendCommand("volm=1");
      volumeChanging = true;
      volumeChangeTime = millis();
      anyButtonPressed = true;
    }

    // CENTER button - short press: toggle play/pause, long press: switch radio
    if (curCenter == LOW) {
      // Button is pressed
      if (centerPressReleased) {
        // Just pressed - record start time
        centerPressStartTime = millis();
        centerPressReleased = false;
        centerActionExecuted = false;
      } else {
        // Still pressed - check if long press time reached
        unsigned long pressDuration = millis() - centerPressStartTime;
        if (pressDuration >= LONG_PRESS_TIME && !centerActionExecuted && NUM_RADIOS > 1) {
          // Long press detected - switch to next radio
          DPRINTLN("CENTER long press - switching radio");
          switchToRadio((currentRadio + 1) % NUM_RADIOS);
          centerActionExecuted = true;
          anyButtonPressed = true;
        }
      }
    } else {
      // Button is released
      if (!centerPressReleased) {
        // Just released
        if (!centerActionExecuted) {
          // Short press - toggle play/pause
          DPRINTLN("CENTER short press - toggle");
          sendCommand("toggle=1");
          anyButtonPressed = true;
        }
        centerPressReleased = true;
      }
    }

    // LEFT button - previous station
    if (curLeft == LOW && lastLeftState == HIGH) {
      sendCommand("prev=1");
      anyButtonPressed = true;
    }

    // RIGHT button - next station
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
  unsigned long inactivityTime = millis() - lastActivityTime;

  // Sprawdź status playera i wybierz odpowiedni timeout
  bool playerStopped = (!playerwrap.isEmpty() &&
                       (playerwrap == "stop" ||
                       playerwrap == "pause" ||
                       playerwrap == "stopped" ||
                       playerwrap == "paused"));
  unsigned long timeoutMs = playerStopped ? (DEEP_SLEEP_TIMEOUT_STOPPED_SEC * 1000) : (DEEP_SLEEP_TIMEOUT_SEC * 1000);

  if (inactivityTime > timeoutMs) {
    if (playerStopped) {
      DPRINTLN("Deep sleep triggered: Player stopped/paused timeout");
    } else {
      DPRINTLN("Deep sleep triggered: General inactivity timeout");
    }
    enterDeepSleep();
    // Kod poniżej nie zostanie wykonany
  }
}
