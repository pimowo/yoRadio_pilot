#include <Arduino.h>

// ========== USTAWIENIA UŻYTKOWNIKA ==========
#include "myoptions.h"

// ========== BIBLIOTEKI ==========
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include "font5x7.h"

//==================================================================================================
// COMPILE-TIME STRING HASHING (CPU optimization - zero runtime overhead)
//==================================================================================================

// FNV-1a hash function (constexpr = compile-time)
constexpr uint32_t fnv1a_hash(const char* str, uint32_t hash = 2166136261u) {
  return *str ? fnv1a_hash(str + 1, (hash ^ static_cast<uint32_t>(*str)) * 16777619u) : hash;
}

// Hash literal operator
constexpr uint32_t operator"" _hash(const char* str, size_t) {
  return fnv1a_hash(str);
}

// Pre-computed WebSocket ID hashes
namespace WSHash {
  constexpr uint32_t nameset    = "nameset"_hash;
  constexpr uint32_t meta       = "meta"_hash;
  constexpr uint32_t volume     = "volume"_hash;
  constexpr uint32_t bitrate    = "bitrate"_hash;
  constexpr uint32_t fmt        = "fmt"_hash;
  constexpr uint32_t playerwrap = "playerwrap"_hash;
}

//==================================================================================================
// BRANCH PREDICTION HINTS (help CPU predict branches)
//==================================================================================================

#ifdef __GNUC__
  #define LIKELY(x)   __builtin_expect(!!(x), 1)
  #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
  #define LIKELY(x)   (x)
  #define UNLIKELY(x) (x)
#endif

//==================================================================================================
// FIRMWARE
//==================================================================================================
#define FIRMWARE_VERSION "1.3"

//==================================================================================================
// TECHNICAL CONSTANTS (nie zmieniaj bez potrzeby)
//==================================================================================================

// Display refresh
#define DISPLAY_REFRESH_RATE_MS 50       // Throttling odświeżania ekranu (ms)

// Battery
#define BATTERY_ADC_PIN ADC1_CHANNEL_0   // Pin ADC (GPIO0 = ADC1_CHANNEL_0)
#define BATTERY_LOW_BLINK_MS 500         // Mruganie ikony baterii (ms)

// Button pins (ESP32-C3 Super Mini)
#define BTN_UP     2                     // GPIO - przycisk GÓRA
#define BTN_RIGHT  3                     // GPIO - przycisk PRAWO
#define BTN_CENTER 4                     // GPIO - przycisk CENTER (wakeup!)
#define BTN_LEFT   5                     // GPIO - przycisk LEWO
#define BTN_DOWN   6                     // GPIO - przycisk DÓŁ

// Volume control timing
#define VOLUME_REPEAT_DELAY_MS 250       // Opóźnienie przed auto-repeat (ms)
#define VOLUME_REPEAT_RATE_MS 80         // Szybkość auto-repeat (ms)

// Watchdog
#define WDT_TIMEOUT 30                   // Timeout watchdog (sekundy)

//==================================================================================================
// PROGMEM STRINGS (Flash instead of RAM)
//==================================================================================================

const char STR_GLOSNOSC[] PROGMEM = "GŁOŚNOŚĆ";
const char STR_LACZE_YORADIO[] PROGMEM = "Łączę z yoRadio";
const char STR_BRAK_WIFI[] PROGMEM = "Brak WiFi";
const char STR_LACZE[] PROGMEM = "[Łączę]";
const char STR_ZATRZYMANY[] PROGMEM = "[Zatrzymany]";
const char STR_PAUZA[] PROGMEM = "[Pauza]";
const char STR_BLAD[] PROGMEM = "[Błąd]";
const char scrollSuffix[] PROGMEM = " * ";

//==================================================================================================
// LAYOUT CONSTANTS (magic numbers → named constants)
//==================================================================================================

// Display layout
#define HEADER_HEIGHT 16
#define FOOTER_Y 52
#define FOOTER_HEIGHT 12

// Battery indicator
#define BAT_X 25
#define BAT_WIDTH 20
#define BAT_HEIGHT 8
#define BAT_TIP_WIDTH 2
#define BAT_TIP_HEIGHT 4

// Volume indicator  
#define VOL_X 54
#define VOL_ICON_SIZE 8
#define VOL_TEXT_OFFSET 10

// Bitrate/Format indicator
#define BITRATE_X 85

// Centered text positions
#define WIFI_ANIM_Y 15
#define WIFI_SSID_Y 35
#define VERSION_Y 52
#define YORADIO_TEXT_Y 20
#define YORADIO_DOTS_Y 32
#define YORADIO_IP_Y 52
#define ERROR_ICON_Y 10
#define ERROR_TEXT_Y 30

// Volume screen
#define VOL_SCREEN_HEADER_HEIGHT 16
#define VOL_SCREEN_NUMBER_Y 25
#define VOL_SCREEN_IP_Y 54

// Animation
#define WIFI_ANIM_INTERVAL_MS 250
#define YORADIO_ANIM_INTERVAL_MS 400
#define BLINK_INTERVAL_MS 1000

//==================================================================================================
// DEBUG HELPERS
//==================================================================================================
#if DEBUG_UART
  #define DPRINT(x) Serial.print(x)
  #define DPRINTLN(x) Serial.println(x)
  #define DPRINTF(fmt, ...) Serial.printf((fmt), __VA_ARGS__)
#else
  #define DPRINT(x)
  #define DPRINTLN(x)
  #define DPRINTF(fmt, ...)
#endif

//==================================================================================================
// HARDWARE CONFIGURATION
//==================================================================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebSocketsClient webSocket;

//==================================================================================================
// GLOBAL VARIABLES
//==================================================================================================

// Data from yoRadio (char[] to avoid String fragmentation)
char stacja[128] = "";
char wykonawca[128] = "";
char utwor[128] = "";
char prev_stacja[128] = "";
char prev_wykonawca[128] = "";
char prev_utwor[128] = "";

int batteryPercent = 100;
int volume = 0;
int bitrate = 0;
char fmt[16] = "";
char playerwrap[16] = "";
bool showCreatorLine = true;

// WebSocket state
bool wsConnected = false;
unsigned long lastWebSocketMessage = 0;
int wsReconnectCount = 0;
const int maxWsReconnects = 10;
unsigned long lastWsReconnectAttempt = 0;

// Timing
unsigned long lastButtonCheck = 0;
unsigned long lastActivityTime = 0;
unsigned long lastDisplayUpdate = 0;

// Volume control
unsigned long volumeUpPressTime = 0;
unsigned long volumeDownPressTime = 0;
unsigned long lastVolumeCommandTime = 0;
bool volumeChanging = false;
unsigned long volumeChangeTime = 0;
const unsigned long VOLUME_DISPLAY_TIME = 2000;

// First data received flag
bool firstDataReceived = false;

// Icons
const unsigned char speakerIcon[] PROGMEM = {
  0b00011000, 0b00111000, 0b11111100, 0b11111100,
  0b11111100, 0b00111000, 0b00011000, 0b00001000
};

const unsigned char wifiErrorIcon[] PROGMEM = {
  0b00011000, 0b00100100, 0b01000010, 0b01000010,
  0b01000010, 0b00100100, 0b00011000, 0b00001000
};

// WiFi state
enum WifiState { WIFI_CONNECTING, WIFI_ERROR, WIFI_OK };
WifiState wifiState = WIFI_CONNECTING;

unsigned long wifiTimer = 0;
const unsigned long wifiTimeout = 15000;

unsigned long wifiRetryTimer = 0;
const unsigned long wifiRetryInterval = 5000;
int wifiRetryCount = 0;
const int maxWifiRetries = 5;

// WebSocket state
enum WsState { WS_DISCONNECTED, WS_CONNECTING, WS_CONNECTED };
WsState wsState = WS_DISCONNECTED;

// Mutex for WebSocket data synchronization
portMUX_TYPE wsMux = portMUX_INITIALIZER_UNLOCKED;

// ADC calibration
esp_adc_cal_characteristics_t adc_chars;

//==================================================================================================
// SCROLL CONFIGURATION
//==================================================================================================

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

struct ScrollState {
  int pos;
  unsigned long t_last;
  unsigned long t_start;
  bool scrolling;
  bool isMoving;
  char text[300];  // Zmniejszone z 384 → 300 (optymalizacja pamięci)
  int singleTextWidth;
  int suffixWidth;
};

ScrollState scrollStates[3];

int activeScrollLine = 0;

//==================================================================================================
// BUTTON STATE MANAGEMENT
//==================================================================================================

struct ButtonState {
  bool lastCenter;
  bool lastLeft;
  bool lastRight;
  
  ButtonState() : lastCenter(false), lastLeft(false), lastRight(false) {}
};

ButtonState buttonState;

//==================================================================================================
// UTF-8 POLISH CHARACTER MAPPING (optimized with lookup table)
//==================================================================================================

// Lookup table for UTF-8 Polish characters (faster than switch)
struct Utf8Mapping {
  uint16_t unicode;
  uint8_t mapped;
};

// Sorted by unicode for binary search (if needed in future)
constexpr Utf8Mapping utf8PolishTable[] PROGMEM = {
  {0x0104, 0xB7}, // Ą
  {0x0105, 0xB8}, // ą
  {0x0106, 0xC4}, // Ć
  {0x0107, 0xBD}, // ć
  {0x0118, 0x90}, // Ę
  {0x0119, 0xD6}, // ę
  {0x0141, 0xD0}, // Ł
  {0x0142, 0xCF}, // ł
  {0x0143, 0xC1}, // Ń
  {0x0144, 0xC0}, // ń
  {0x00D3, 0xBF}, // Ó
  {0x00F3, 0xBE}, // ó
  {0x015A, 0xCC}, // Ś
  {0x015B, 0xCB}, // ś
  {0x0179, 0xBC}, // Ź
  {0x017A, 0xBB}, // ź
  {0x017B, 0xBA}, // Ż
  {0x017C, 0xB9}, // ż
};

constexpr size_t utf8PolishTableSize = sizeof(utf8PolishTable) / sizeof(utf8PolishTable[0]);

// Fast lookup (linear search is OK for 18 items, ~9 iterations on average)
__attribute__((always_inline))
inline uint8_t mapUtf8Polish(uint16_t unicode) {
  for (size_t i = 0; i < utf8PolishTableSize; i++) {
    Utf8Mapping mapping;
    memcpy_P(&mapping, &utf8PolishTable[i], sizeof(Utf8Mapping));
    if (mapping.unicode == unicode) {
      return mapping.mapped;
    }
  }
  return 0;
}

//==================================================================================================
// CUSTOM FONT DRAWING FUNCTIONS
//==================================================================================================

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

void drawString5x7(int16_t x, int16_t y, const char* s, uint8_t scale = 1, uint16_t color = SSD1306_WHITE) {
  int16_t cx = x;
  for (size_t i = 0; s[i] != '\0';) {
    uint8_t c = (uint8_t)s[i];
    if (c < 128) {
      if (c >= 32) drawChar5x7(cx, y, c, color, scale);
      cx += (5 + 1) * scale;
      i++;
    } else {
      if ((c & 0xE0) == 0xC0 && s[i+1] != '\0') {
        uint8_t c2 = (uint8_t)s[i+1];
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

int getPixelWidth5x7(const char* s, uint8_t scale = 1) {
  int glyphs = 0;
  for (size_t i = 0; s[i] != '\0';) {
    uint8_t c = (uint8_t)s[i];
    if (c < 128) { 
      glyphs++; 
      i++; 
    }
    else if ((c & 0xE0) == 0xC0 && s[i+1] != '\0') { 
      glyphs++; 
      i += 2; 
    }
    else {
      i++;
    }
  }
  return glyphs * (5 + 1) * scale;
}

//==================================================================================================
// FORWARD DECLARATIONS
//==================================================================================================

void prepareScroll(int line, const char* txt, int scale);

//==================================================================================================
// HELPER FUNCTIONS
//==================================================================================================

// Safe string copy with automatic null-termination (inline for performance)
__attribute__((always_inline))
inline void safeStrCopy(char* dest, const char* src, size_t destSize) {
  if (destSize == 0) return;
  strncpy(dest, src, destSize - 1);
  dest[destSize - 1] = '\0';
}

// Check if text changed and update scroll if needed
bool updateScrollIfChanged(int line, const char* newText, char* prevText, size_t prevSize, int scale) {
  if (strcmp(newText, prevText) != 0) {
    safeStrCopy(prevText, newText, prevSize);
    prepareScroll(line, newText, scale);
    return true;
  }
  return false;
}

// Draw centered text
void drawCenteredText(const char* text, int y, uint8_t scale = 1, uint16_t color = SSD1306_WHITE) {
  int textWidth = getPixelWidth5x7(text, scale);
  int x = (SCREEN_WIDTH - textWidth) / 2;
  drawString5x7(x, y, text, scale, color);
}

// Draw centered text from PROGMEM (memory optimization)
void drawCenteredTextP(const char* textP, int y, uint8_t scale = 1, uint16_t color = SSD1306_WHITE) {
  char buffer[64];
  strncpy_P(buffer, textP, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';
  drawCenteredText(buffer, y, scale, color);
}

// Draw number using custom font (for consistency) - inline for performance
__attribute__((always_inline))
inline void drawNumberCustom(int x, int y, int number, uint8_t scale = 1, uint16_t color = SSD1306_WHITE) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", number);
  drawString5x7(x, y, buf, scale, color);
}

//==================================================================================================
// BATTERY MEASUREMENT
//==================================================================================================

int readBatteryPercent() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(BATTERY_ADC_PIN, ADC_ATTEN_DB_12);
  
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  
  uint32_t voltage = 0;
  for (int i = 0; i < 10; i++) {
    voltage += esp_adc_cal_raw_to_voltage(adc1_get_raw(BATTERY_ADC_PIN), &adc_chars);
    delay(10);
  }
  voltage /= 10;
  
  voltage = (uint32_t)(voltage * BATTERY_DIVIDER_RATIO);
  
  DPRINT("Battery voltage: ");
  DPRINT(voltage);
  DPRINTLN(" mV");
  
  if (voltage >= BATTERY_MAX_MV) return 100;
  if (voltage <= BATTERY_MIN_MV) return 0;
  
  int percent = map(voltage, BATTERY_MIN_MV, BATTERY_MAX_MV, 0, 100);
  return constrain(percent, 0, 100);
}

//==================================================================================================
// SCROLL FUNCTIONS
//==================================================================================================

void prepareScroll(int line, const char* txt, int scale) {
  int singleWidth = getPixelWidth5x7(txt, scale);
  int availWidth = scrollConfs[line].width;

  bool needsScroll = singleWidth > availWidth;

  scrollStates[line].singleTextWidth = singleWidth;
  scrollStates[line]. pos = 0;
  scrollStates[line].t_start = millis();
  scrollStates[line].t_last = millis();
  scrollStates[line].isMoving = false;

  if (needsScroll) {
    // Load suffix from PROGMEM
    char suffix[4];
    strcpy_P(suffix, scrollSuffix);
    int suffixWidth = getPixelWidth5x7(suffix, scale);
    
    snprintf(scrollStates[line]. text, sizeof(scrollStates[line].text), 
             "%s%s%s", txt, suffix, txt);
    scrollStates[line].suffixWidth = suffixWidth;
    scrollStates[line].scrolling = true;
  } else {
    safeStrCopy(scrollStates[line].text, txt, sizeof(scrollStates[line].text));
    scrollStates[line].suffixWidth = 0;
    scrollStates[line]. scrolling = false;
  }
}

void updateScroll(int line) {
  unsigned long now = millis();
  auto& conf = scrollConfs[line];
  auto& state = scrollStates[line];

  // Fast path:  non-scrolling lines
  if (UNLIKELY(!state.scrolling)) {
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
    drawString5x7(x, y, state. text, scale, SSD1306_WHITE);
  }
}

//==================================================================================================
// DISPLAY SCREEN FUNCTIONS
//==================================================================================================

void drawRssiBottom() {
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
      display.fillRect(x, FOOTER_Y + (8 - barHeights[i]), barWidth, barHeights[i], SSD1306_WHITE);
    else
      display. drawFastHLine(x, FOOTER_Y + (8 - barHeights[i]), barWidth, SSD1306_WHITE);
  }
}

void drawBatteryIndicator() {
  bool showBattery = true;
  if (batteryPercent < 20) {
    showBattery = ((millis() / BATTERY_LOW_BLINK_MS) % 2) == 0;
  }

  if (showBattery) {
    display.drawRect(BAT_X, FOOTER_Y, BAT_WIDTH, BAT_HEIGHT, SSD1306_WHITE);
    display.fillRect(BAT_X + BAT_WIDTH, FOOTER_Y + BAT_TIP_HEIGHT / 2, 
                     BAT_TIP_WIDTH, BAT_HEIGHT - BAT_TIP_HEIGHT, SSD1306_WHITE);
    
    int fillWidth = (batteryPercent * (BAT_WIDTH - 2)) / 100;
    if (fillWidth > 0) {
      display.fillRect(BAT_X + 1, FOOTER_Y + 1, fillWidth, BAT_HEIGHT - 2, SSD1306_WHITE);
    }
  }
}

void drawVolumeIndicator() {
  display.drawBitmap(VOL_X, FOOTER_Y, speakerIcon, VOL_ICON_SIZE, VOL_ICON_SIZE, SSD1306_WHITE);
  drawNumberCustom(VOL_X + VOL_TEXT_OFFSET, FOOTER_Y, volume, 1, SSD1306_WHITE);
}

// Optimized:  no mutex, data passed as parameters
void drawBitrateFormat(bool isPlaying, int localBitrate, const char* localFmt) {
  if (isPlaying) {
    int cursorX = BITRATE_X;
    
    if (localBitrate > 0) {
      drawNumberCustom(cursorX, FOOTER_Y, localBitrate, 1, SSD1306_WHITE);
      
      char bitrateStr[6];
      snprintf(bitrateStr, sizeof(bitrateStr), "%d", localBitrate);
      cursorX += getPixelWidth5x7(bitrateStr, 1);
    }
    
    if (localFmt[0] != '\0') {
      if (localBitrate > 0 && localBitrate < 1000) {
        drawString5x7(cursorX, FOOTER_Y, " ", 1, SSD1306_WHITE);
        cursorX += 6;
      }
      drawString5x7(cursorX, FOOTER_Y, localFmt, 1, SSD1306_WHITE);
    }
  }
}

void drawVolumeScreen() {
  display.fillRect(0, 0, SCREEN_WIDTH, VOL_SCREEN_HEADER_HEIGHT, SSD1306_WHITE);
  
  drawCenteredTextP(STR_GLOSNOSC, 1, 2, SSD1306_BLACK);

  char volText[4];
  snprintf(volText, sizeof(volText), "%d", volume);
  drawCenteredText(volText, VOL_SCREEN_NUMBER_Y, 3, SSD1306_WHITE);

  char ipText[24];
  IPAddress ip = WiFi.localIP();
  snprintf(ipText, sizeof(ipText), "IP:%d.%d.%d. %d", ip[0], ip[1], ip[2], ip[3]);
  drawString5x7(2, VOL_SCREEN_IP_Y, ipText, 1, SSD1306_WHITE);
}

void drawWifiConnectingScreen() {
  static unsigned long lastAnim = 0;
  static int animStep = 1;
  
  if (millis() - lastAnim > WIFI_ANIM_INTERVAL_MS) {
    lastAnim = millis();
    animStep++;
    if (animStep > 4) animStep = 1;
  }

  int barWidth = 4;
  int barSpacing = 2;
  int barHeights[4] = {2, 4, 6, 8};
  int totalWidth = animStep * (barWidth + barSpacing) - barSpacing;
  int startX = (SCREEN_WIDTH - totalWidth) / 2;

  for (int i = 0; i < animStep; i++) {
    int x = startX + i * (barWidth + barSpacing);
    display.fillRect(x, WIFI_ANIM_Y + (8 - barHeights[i]), barWidth, barHeights[i], SSD1306_WHITE);
  }

  drawCenteredText(WIFI_SSID, WIFI_SSID_Y, 1, SSD1306_WHITE);

  char versionText[8];
  snprintf(versionText, sizeof(versionText), "v%s", FIRMWARE_VERSION);
  drawCenteredText(versionText, VERSION_Y, 1, SSD1306_WHITE);
}

void drawWifiErrorScreen() {
  bool blink = (millis() % BLINK_INTERVAL_MS < 500);

  int iconX = (SCREEN_WIDTH - 8) / 2;
  if (blink) {
    display.drawBitmap(iconX, ERROR_ICON_Y, wifiErrorIcon, 8, 8, SSD1306_WHITE);
  }

  drawCenteredTextP(STR_BRAK_WIFI, ERROR_TEXT_Y, 1, SSD1306_WHITE);
}

void drawYoRadioConnectingScreen() {
  static unsigned long lastAnim = 0;
  static int dotCount = 1;
  
  if (millis() - lastAnim > YORADIO_ANIM_INTERVAL_MS) {
    lastAnim = millis();
    dotCount++;
    if (dotCount > 3) dotCount = 1;
  }

  drawCenteredTextP(STR_LACZE_YORADIO, YORADIO_TEXT_Y, 1, SSD1306_WHITE);

  char dots[5] = "";
  for (int i = 0; i < dotCount; i++) {
    strcat(dots, ".");
  }
  drawCenteredText(dots, YORADIO_DOTS_Y, 2, SSD1306_WHITE);

  char ipText[32];
  snprintf(ipText, sizeof(ipText), "IP: %s", YORADIO_IP);
  drawCenteredText(ipText, YORADIO_IP_Y, 1, SSD1306_WHITE);
}

void drawMainScreen() {
  display.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, SSD1306_WHITE);

  // ========== ATOMIC READ - Fast mutex lock/unlock ==========
  char localStacja[128];
  char localWykonawca[128];
  char localUtwor[128];
  char localFmt[16];
  int localBitrate;
  bool isPlaying;
  
  portENTER_CRITICAL(&wsMux);
  safeStrCopy(localStacja, stacja, sizeof(localStacja));
  safeStrCopy(localWykonawca, wykonawca, sizeof(localWykonawca));
  safeStrCopy(localUtwor, utwor, sizeof(localUtwor));
  safeStrCopy(localFmt, fmt, sizeof(localFmt));
  localBitrate = bitrate;
  isPlaying = (playerwrap[0] != '\0' &&
               strcmp(playerwrap, "stop") != 0 &&
               strcmp(playerwrap, "pause") != 0 &&
               strcmp(playerwrap, "stopped") != 0 &&
               strcmp(playerwrap, "paused") != 0);
  bool oldShowCreatorLine = showCreatorLine;
  portEXIT_CRITICAL(&wsMux);
  
  // ========== Work with local copies (no mutex) ==========
  
  updateScrollIfChanged(0, localStacja, prev_stacja, sizeof(prev_stacja), scrollConfs[0].fontsize);

  if (localWykonawca[0] == '\0') {
    updateScrollIfChanged(1, localUtwor, prev_utwor, sizeof(prev_utwor), scrollConfs[1].fontsize);
    showCreatorLine = false;
  } else {
    updateScrollIfChanged(1, localWykonawca, prev_wykonawca, sizeof(prev_wykonawca), scrollConfs[1].fontsize);
    updateScrollIfChanged(2, localUtwor, prev_utwor, sizeof(prev_utwor), scrollConfs[2].fontsize);
    showCreatorLine = true;
  }

  if (oldShowCreatorLine != showCreatorLine) {
    activeScrollLine = 0;
    unsigned long now = millis();
    for (int i = 0; i < (showCreatorLine ? 3 :  2); i++) {
      scrollStates[i].t_start = now;
      scrollStates[i].t_last = now;
      scrollStates[i].pos = 0;
      scrollStates[i].isMoving = false;
    }
  } else {
    if (! showCreatorLine && activeScrollLine > 1) {
      activeScrollLine = 0;
    }
  }

  // ========== FAST PATH:  Update & draw only visible lines ==========
  
  updateScroll(0);
  drawScrollLine(0, scrollConfs[0].fontsize);
  
  if (showCreatorLine) {
    updateScroll(1);
    updateScroll(2);
    drawScrollLine(1, scrollConfs[1].fontsize);
    drawScrollLine(2, scrollConfs[2].fontsize);
  } else {
    updateScroll(1);
    drawScrollLine(1, scrollConfs[1].fontsize);
  }

  // Footer
  drawRssiBottom();
  drawBatteryIndicator();
  drawVolumeIndicator();
  drawBitrateFormat(isPlaying, localBitrate, localFmt);
}

//==================================================================================================
// MAIN DISPLAY UPDATE
//==================================================================================================

void updateDisplay() {
  if (UNLIKELY((millis() - lastDisplayUpdate) < DISPLAY_REFRESH_RATE_MS)) {
    return;
  }
  lastDisplayUpdate = millis();

  display.clearDisplay();

  if (volumeChanging && ((millis() - volumeChangeTime) >= VOLUME_DISPLAY_TIME)) {
    volumeChanging = false;
  }

  // ========== SCREEN ROUTING ==========
  
  if (volumeChanging) {
    drawVolumeScreen();
  }
  else if (wifiState == WIFI_CONNECTING) {
    drawWifiConnectingScreen();
  }
  else if (wifiState == WIFI_ERROR) {
    drawWifiErrorScreen();
  }
  else if (wifiState == WIFI_OK && ! firstDataReceived) {
    drawYoRadioConnectingScreen();
  }
  else if (wifiState == WIFI_OK && firstDataReceived) {
    drawMainScreen();
  }

  display.display();
}

//==================================================================================================
// WEBSOCKET EVENT HANDLER
//==================================================================================================

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    DPRINTLN("WebSocket connected!");
    portENTER_CRITICAL(&wsMux);
    wsConnected = true;
    wsState = WS_CONNECTING;
    lastWebSocketMessage = millis();
    wsReconnectCount = 0;
    portEXIT_CRITICAL(&wsMux);
    webSocket.sendTXT("getindex=1");
    return;
  }

  if (type == WStype_TEXT) {
    portENTER_CRITICAL(&wsMux);
    lastWebSocketMessage = millis();
    
    if (! firstDataReceived) {
      firstDataReceived = true;
      wsState = WS_CONNECTED;
    }
    portEXIT_CRITICAL(&wsMux);
    
    DPRINT("WebSocket message: ");
    DPRINTLN((char*)payload);

    // Static allocation - saves 1KB stack per call
    static StaticJsonDocument<1024> doc;
    doc. clear();
    
    DeserializationError error = deserializeJson(doc, payload, length);
    
    if (error) {
      DPRINT("JSON parse error: ");
      DPRINTLN(error.c_str());
      return;
    }
    
    if (doc. overflowed()) {
      DPRINTLN("JSON buffer overflow!");
      return;
    }

    if (doc. containsKey("payload")) {
      JsonArray arr = doc["payload"]. as<JsonArray>();
      
      portENTER_CRITICAL(&wsMux);
      
      for (JsonObject obj : arr) {
        const char* id = obj["id"];
        uint32_t idHash = fnv1a_hash(id);
        
        switch (idHash) {
          case WSHash:: nameset:  {
            const char* value = obj["value"];
            safeStrCopy(stacja, value, sizeof(stacja));
            break;
          }
          
          case WSHash::meta: {
            const char* metaStr = obj["value"];
            
            if (strstr(metaStr, "[connecting]") != nullptr ||
                strstr(metaStr, "[łącze]") != nullptr ||
                strstr(metaStr, "[loading]") != nullptr ||
                strstr(metaStr, "[buffering]") != nullptr) {
              wykonawca[0] = '\0';
              strcpy_P(utwor, STR_LACZE);
            }
            else if (strstr(metaStr, "[stopped]") != nullptr ||
                     strstr(metaStr, "[zatrzymany]") != nullptr ||
                     strstr(metaStr, "[stop]") != nullptr) {
              wykonawca[0] = '\0';
              strcpy_P(utwor, STR_ZATRZYMANY);
            }
            else if (strstr(metaStr, "[paused]") != nullptr ||
                     strstr(metaStr, "[pauza]") != nullptr) {
              wykonawca[0] = '\0';
              strcpy_P(utwor, STR_PAUZA);
            }
            else if (strstr(metaStr, "[error]") != nullptr ||
                     strstr(metaStr, "[błąd]") != nullptr) {
              wykonawca[0] = '\0';
              strcpy_P(utwor, STR_BLAD);
            }
            else {
              const char* sep = strstr(metaStr, " - ");
              if (sep != nullptr) {
                size_t len = sep - metaStr;
                len = (len < sizeof(wykonawca) - 1) ? len : sizeof(wykonawca) - 1;
                strncpy(wykonawca, metaStr, len);
                wykonawca[len] = '\0';
                safeStrCopy(utwor, sep + 3, sizeof(utwor));
              } else {
                wykonawca[0] = '\0';
                safeStrCopy(utwor, metaStr, sizeof(utwor));
              }
            }
            break;
          }
          
          case WSHash::volume: {
            volume = obj["value"];
            break;
          }
          
          case WSHash::bitrate: {
            if (obj["value"]. is<int>()) {
              bitrate = obj["value"];
            }
            break;
          }
          
          case WSHash::fmt: {
            const char* fmtValue = obj["value"];
            if (strcmp(fmtValue, "bitrate") == 0) {
              strcpy(fmt, "kbs");
            } else {
              safeStrCopy(fmt, fmtValue, sizeof(fmt));
            }
            break;
          }
          
          case WSHash::playerwrap: {
            const char* value = obj["value"];
            safeStrCopy(playerwrap, value, sizeof(playerwrap));
            break;
          }
          
          default: 
            break;
        }
      }
      
      portEXIT_CRITICAL(&wsMux);
    }

    if (wifiState == WIFI_OK) updateDisplay();
  }

  if (type == WStype_DISCONNECTED) {
    DPRINTLN("WebSocket disconnected!");
    portENTER_CRITICAL(&wsMux);
    wsConnected = false;
    portEXIT_CRITICAL(&wsMux);
  }
}

//==================================================================================================
// COMMAND FUNCTIONS
//==================================================================================================

void sendCommand(const char* cmd) {
  if (webSocket.isConnected()) {
    webSocket.sendTXT(cmd);
    DPRINT("Sent: ");
    DPRINTLN(cmd);
  } else {
    DPRINT("Attempt to send while WS disconnected:  ");
    DPRINTLN(cmd);
  }
}

void enterDeepSleep() {
  DPRINTLN("Preparing deep sleep...");
  display.clearDisplay();
  display.display();
  display.ssd1306_command(0xAE);
  delay(100);

  webSocket.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  #if BTN_CENTER < 0 || BTN_CENTER > 5
    #error "BTN_CENTER must be GPIO 0-5 (RTC GPIO) for ESP32-C3!"
  #endif
  
  DPRINTLN("Configuring deep sleep GPIO wakeup on GPIO4 (LOW level)...");
  
  esp_err_t err = esp_deep_sleep_enable_gpio_wakeup(1ULL << BTN_CENTER, ESP_GPIO_WAKEUP_GPIO_LOW);
  
  if (err != ESP_OK) {
    DPRINT("GPIO wakeup config failed: ");
    DPRINTLN(err);
  }
  
  DPRINTLN("Entering deep sleep in 50 ms...");
  Serial.flush();
  delay(50);

  esp_deep_sleep_start();
}

void oledSetContrast(uint8_t c) {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(c);
}

#if DEBUG_UART
void printWiFiStatus() {
  wl_status_t status = WiFi.status();
  DPRINT("WiFi Status: ");
  switch(status) {
    case WL_IDLE_STATUS:  DPRINTLN("IDLE"); break;
    case WL_NO_SSID_AVAIL: DPRINTLN("NO SSID AVAILABLE"); break;
    case WL_SCAN_COMPLETED:  DPRINTLN("SCAN COMPLETED"); break;
    case WL_CONNECTED: DPRINTLN("CONNECTED"); break;
    case WL_CONNECT_FAILED: DPRINTLN("CONNECT FAILED"); break;
    case WL_CONNECTION_LOST: DPRINTLN("CONNECTION LOST"); break;
    case WL_DISCONNECTED: DPRINTLN("DISCONNECTED"); break;
    default: DPRINTF("UNKNOWN (%d)\n", status); break;
  }
  if (status == WL_CONNECTED) {
    DPRINT("RSSI: ");
    DPRINT(WiFi.RSSI());
    DPRINTLN(" dBm");
  }
}
#endif

void connectWiFi() {
  DPRINTLN("Attempting WiFi connection...");
  
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  
  #if USE_STATIC_IP
    IPAddress staticIP, gateway, subnet, dns1, dns2;
    staticIP. fromString(STATIC_IP);
    gateway.fromString(GATEWAY_IP);
    subnet.fromString(SUBNET_MASK);
    dns1.fromString(DNS1_IP);
    dns2.fromString(DNS2_IP);
    
    if (! WiFi.config(staticIP, gateway, subnet, dns1, dns2)) {
      DPRINTLN("Static IP config failed!  Falling back to DHCP");
    } else {
      DPRINT("Static IP configured: ");
      DPRINTLN(staticIP);
    }
  #else
    DPRINTLN("Using DHCP");
  #endif
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiTimer = millis();
  wifiRetryTimer = millis();
  wifiState = WIFI_CONNECTING;
  
  DPRINT("Connecting to:  ");
  DPRINTLN(WIFI_SSID);
}

//==================================================================================================
// BUTTON HANDLER
//==================================================================================================

void handleButtons() {
  bool curUp = (digitalRead(BTN_UP) == LOW);
  bool curDown = (digitalRead(BTN_DOWN) == LOW);
  bool curCenter = (digitalRead(BTN_CENTER) == LOW);
  bool curLeft = (digitalRead(BTN_LEFT) == LOW);
  bool curRight = (digitalRead(BTN_RIGHT) == LOW);

  bool anyButtonPressed = false;

  // ========== VOLUME UP (auto-repeat) ==========
  if (curUp) {
    if (volumeUpPressTime == 0) {
      sendCommand("volp=1");
      volumeUpPressTime = millis();
      lastVolumeCommandTime = millis();
      volumeChanging = true;
      volumeChangeTime = millis();
      anyButtonPressed = true;
    } else {
      unsigned long pressDuration = millis() - volumeUpPressTime;
      if (pressDuration >= VOLUME_REPEAT_DELAY_MS) {
        if ((millis() - lastVolumeCommandTime) >= VOLUME_REPEAT_RATE_MS) {
          sendCommand("volp=1");
          lastVolumeCommandTime = millis();
          volumeChangeTime = millis();
          anyButtonPressed = true;
        }
      }
    }
  } else {
    volumeUpPressTime = 0;
  }

  // ========== VOLUME DOWN (auto-repeat) ==========
  if (curDown) {
    if (volumeDownPressTime == 0) {
      sendCommand("volm=1");
      volumeDownPressTime = millis();
      lastVolumeCommandTime = millis();
      volumeChanging = true;
      volumeChangeTime = millis();
      anyButtonPressed = true;
    } else {
      unsigned long pressDuration = millis() - volumeDownPressTime;
      if (pressDuration >= VOLUME_REPEAT_DELAY_MS) {
        if ((millis() - lastVolumeCommandTime) >= VOLUME_REPEAT_RATE_MS) {
          sendCommand("volm=1");
          lastVolumeCommandTime = millis();
          volumeChangeTime = millis();
          anyButtonPressed = true;
        }
      }
    }
  } else {
    volumeDownPressTime = 0;
  }

  // ========== CENTER (toggle - debounced) ==========
  if (curCenter && !buttonState.lastCenter) {
    sendCommand("toggle=1");
    anyButtonPressed = true;
  }
  buttonState.lastCenter = curCenter;

  // ========== LEFT (prev - debounced) ==========
  if (curLeft && !buttonState.lastLeft) {
    sendCommand("prev=1");
    anyButtonPressed = true;
  }
  buttonState.lastLeft = curLeft;

  // ========== RIGHT (next - debounced) ==========
  if (curRight && !buttonState.lastRight) {
    sendCommand("next=1");
    anyButtonPressed = true;
  }
  buttonState.lastRight = curRight;

  if (UNLIKELY(anyButtonPressed)) {
    lastActivityTime = millis();
  }
}

//==================================================================================================
// SETUP
//==================================================================================================

void setup() {
  Serial.begin(115200);
  delay(100);

  for (int i = 0; i < 3; ++i) {
    scrollStates[i]. pos = 0;
    scrollStates[i].t_last = 0;
    scrollStates[i].t_start = 0;
    scrollStates[i].scrolling = false;
    scrollStates[i].isMoving = false;
    scrollStates[i].text[0] = '\0';
    scrollStates[i].singleTextWidth = 0;
    scrollStates[i].suffixWidth = 0;
  }

  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  switch (wakeupReason) {
    case ESP_SLEEP_WAKEUP_GPIO:  DPRINTLN("Wakeup:  GPIO (button pressed)"); break;
    case ESP_SLEEP_WAKEUP_TIMER: DPRINTLN("Wakeup:  TIMER"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: DPRINTLN("Wakeup: TOUCHPAD"); break;
    case ESP_SLEEP_WAKEUP_ULP: DPRINTLN("Wakeup: ULP"); break;
    case ESP_SLEEP_WAKEUP_UNDEFINED: DPRINTLN("Wakeup: normal boot / power on"); break;
    default: DPRINTF("Wakeup: %d\n", wakeupReason); break;
  }

  DPRINT("\n\nStarting YoRadio OLED Display v");
  DPRINTLN(FIRMWARE_VERSION);
  DPRINTLN("ESP32-C3 Super Mini");
  DPRINT("YoRadio IP: ");
  DPRINTLN(YORADIO_IP);

  DPRINTLN("=== Battery Measurement ===");
  batteryPercent = readBatteryPercent();
  DPRINT("Battery: ");
  DPRINT(batteryPercent);
  DPRINTLN("%");

  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  DPRINTLN("Watchdog timer initialized");

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  if (! display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    DPRINTLN(F("SSD1306 failed"));
    for(;;);
  }

  uint8_t brightness = constrain(OLED_BRIGHTNESS, 0, 15);
  oledSetContrast(brightness * 16);
  DPRINT("OLED brightness: ");
  DPRINTLN(brightness);

  display.clearDisplay();
  display.display();

  DPRINTLN("=== WiFi Initialization ===");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(500);
  
  connectWiFi();

  lastActivityTime = millis();
  lastWebSocketMessage = millis();
  lastDisplayUpdate = millis();
}

//==================================================================================================
// MAIN LOOP
//==================================================================================================

void loop() {
  esp_task_wdt_reset();

  // ========== BUTTONS (highest priority - 20ms) ==========
  if ((millis() - lastButtonCheck) >= 20) {
    lastButtonCheck = millis();
    handleButtons();
  }

  // ========== WEBSOCKET ==========
  webSocket.loop();

  // ========== DISPLAY ==========
  updateDisplay();

  // ========== WIFI STATE MACHINE ==========
  if (wifiState == WIFI_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      DPRINTLN("WiFi connected!");
      DPRINT("IP:  ");
      DPRINTLN(WiFi.localIP());
      DPRINT("Signal: ");
      DPRINT(WiFi.RSSI());
      DPRINTLN(" dBm");
      
      wifiState = WIFI_OK;
      wifiRetryCount = 0;
      
      delay(500);
      
      if (wsReconnectCount < maxWsReconnects) {
        DPRINT("Connecting to WebSocket at ");
        DPRINT(YORADIO_IP);
        DPRINTLN(": 80/ws");
        webSocket.begin(YORADIO_IP, 80, "/ws");
        webSocket.onEvent(webSocketEvent);
        webSocket.setReconnectInterval(5000);
        
        wsReconnectCount++;
        lastWsReconnectAttempt = millis();
        lastWebSocketMessage = millis();
      } else {
        DPRINTLN("Max WebSocket reconnects reached");
        if ((millis() - lastWsReconnectAttempt) > 300000) {
          wsReconnectCount = 0;
          DPRINTLN("Resetting WebSocket reconnect counter");
        }
      }
    } 
    else {
      if ((millis() - wifiTimer) >= wifiTimeout) {
        wifiRetryCount++;
        DPRINT("WiFi attempt ");
        DPRINT(wifiRetryCount);
        DPRINT("/");
        DPRINT(maxWifiRetries);
        DPRINTLN(" failed");
        
        #if DEBUG_UART
          printWiFiStatus();
        #endif
        
        if (wifiRetryCount >= maxWifiRetries) {
          DPRINTLN("Max retries - entering ERROR");
          wifiState = WIFI_ERROR;
          wifiRetryTimer = millis();
        } else {
          DPRINTLN("Retrying.. .");
          WiFi.disconnect();
          delay(100);
          connectWiFi();
        }
      }
    }
  } 
  else if (wifiState == WIFI_OK) {
    if (WiFi.status() != WL_CONNECTED) {
      DPRINTLN("WiFi disconnected!");
      webSocket.disconnect();
      portENTER_CRITICAL(&wsMux);
      wsConnected = false;
      portEXIT_CRITICAL(&wsMux);
      wifiRetryCount = 0;
      connectWiFi();
    }
  } 
  else if (wifiState == WIFI_ERROR) {
    if ((millis() - wifiRetryTimer) >= wifiRetryInterval) {
      DPRINTLN("Retrying from ERROR.. .");
      wifiRetryCount = 0;
      connectWiFi();
    }
  }

  // ========== DEEP SLEEP CHECK ==========
  unsigned long inactivityTime = millis() - lastActivityTime;

  portENTER_CRITICAL(&wsMux);
  bool playerStopped = (playerwrap[0] != '\0' &&
                       (strcmp(playerwrap, "stop") == 0 ||
                       strcmp(playerwrap, "pause") == 0 ||
                       strcmp(playerwrap, "stopped") == 0 ||
                       strcmp(playerwrap, "paused") == 0));
  portEXIT_CRITICAL(&wsMux);
  
  unsigned long timeoutMs = playerStopped ? (DEEP_SLEEP_TIMEOUT_STOPPED_SEC * 1000) : (DEEP_SLEEP_TIMEOUT_SEC * 1000);

  if (inactivityTime >= timeoutMs) {
    if (playerStopped) {
      DPRINTLN("Deep sleep:  player stopped");
    } else {
      DPRINTLN("Deep sleep: inactivity");
    }
    enterDeepSleep();
  }
}