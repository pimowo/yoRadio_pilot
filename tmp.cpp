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
// firmware
#define FIRMWARE_VERSION "1.0"           // wersja oprogramowania

// ====================== USTAWIENIA / SETTINGS ======================
// Debug UART messages:           ustaw na 1 aby włączyć diagnostykę po UART, 0 aby wyłączyć
#define DEBUG_UART 0

// sieć
#define WIFI_SSID "pimowo"               // sieć 
#define WIFI_PASS "ckH59LRZQzCDQFiUgj"   // hasło sieci
#define STATIC_IP "192.168.1.123"        // IP
#define GATEWAY_IP "192.168.1.1"         // brama
#define SUBNET_MASK "255.255.255.0"      // maska
#define DNS1_IP "192.168.1.1"            // DNS 1
#define DNS2_IP "8.8.8.8"                // DNS 2
#define USE_STATIC_IP 1                  // 1 = statyczne IP, 0 = DHCP

// yoRadio
#define YORADIO_IP "192.168.1.122"       // IP adres yoRadio

// uśpienie
#define DEEP_SLEEP_TIMEOUT_SEC 30        // sekundy bezczynności przed deep sleep (podczas odtwarzania)
#define DEEP_SLEEP_TIMEOUT_STOPPED_SEC 15 // sekundy bezczynności przed deep sleep (gdy zatrzymany)

// klawiatura - ESP32-C3 Super Mini GPIO mapping
#define BTN_UP     2                     // pin GÓRA
#define BTN_RIGHT  3                     // pin PRAWO
#define BTN_CENTER 4                     // pin OK (Wakeup pin - musi być RTC GPIO!)
#define BTN_LEFT   5                     // pin LEWO 
#define BTN_DOWN   6                     // pin DÓŁ

// Bateria - pomiar ADC
#define BATTERY_ADC_PIN ADC1_CHANNEL_0   // GPIO0 = ADC1_CH0 na ESP32-C3
#define BATTERY_MIN_MV 3000              // 3.0V = pusta bateria
#define BATTERY_MAX_MV 4200              // 4.2V = pełna bateria
#define BATTERY_DIVIDER_RATIO 2.0        // Jeśli używasz dzielnika napięcia (R1=R2)

// wyświetlacz - ESP32-C3 I2C:           SDA=GPIO8, SCL=GPIO9
#define OLED_BRIGHTNESS 10               // 0-15 (wartość * 16 daje zakres 0-240 dla kontrastu SSD1306)
#define DISPLAY_REFRESH_RATE_MS 50       // odświeżanie ekranu (50ms = 20 FPS)

// bateria
#define BATTERY_LOW_BLINK_MS 500         // interwał mrugania słabej baterii

// watchdog
#define WDT_TIMEOUT 30                   // timeout watchdog w sekundach

// Volume control
#define VOLUME_REPEAT_DELAY_MS 250       // Opóźnienie przed rozpoczęciem powtarzania
#define VOLUME_REPEAT_RATE_MS 80        // Szybkość powtarzania poleceń głośności
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

// Stałe IP adresy (PROGMEM optimization)
#if USE_STATIC_IP
const IPAddress staticIP(192, 168, 1, 123);
const IPAddress gateway(192, 168, 1, 1);
const IPAddress subnet(255, 255, 255, 0);
const IPAddress dns1(192, 168, 1, 1);
const IPAddress dns2(8, 8, 8, 8);
#endif

// Zmienne z char[] zamiast String (fix memory fragmentation)
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

bool wsConnected = false;
unsigned long lastWebSocketMessage = 0;

unsigned long lastButtonCheck = 0;
unsigned long lastActivityTime = 0;
unsigned long lastDisplayUpdate = 0;

// Volume control timing
unsigned long volumeUpPressTime = 0;
unsigned long volumeDownPressTime = 0;
unsigned long lastVolumeCommandTime = 0;

bool volumeChanging = false;
unsigned long volumeChangeTime = 0;
const unsigned long VOLUME_DISPLAY_TIME = 2000;  // 2 sekundy wyświetlania

// WebSocket reconnect counter
int wsReconnectCount = 0;
const int maxWsReconnects = 10;
unsigned long lastWsReconnectAttempt = 0;

const unsigned char speakerIcon[] PROGMEM = {
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
const unsigned long wifiTimeout = 15000;  // 15 sekund

// WiFi retry mechanism
unsigned long wifiRetryTimer = 0;
const unsigned long wifiRetryInterval = 5000;  // Próbuj ponownie co 5 sekund
int wifiRetryCount = 0;
const int maxWifiRetries = 5;  // Maksymalnie 5 prób przed WIFI_ERROR

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
  char text[384];  // Zmieniono z String na char[] - max 128*2 + separator
  int singleTextWidth;
  int suffixWidth;
};

ScrollState scrollStates[3];

const char* scrollSuffix = " * ";

// ===== SEKWENCYJNE PRZEWIJANIE =====
int activeScrollLine = 0;

// ADC calibration
esp_adc_cal_characteristics_t adc_chars;

// Mutex dla synchronizacji WebSocket (fix race condition)
portMUX_TYPE wsMux = portMUX_INITIALIZER_UNLOCKED;

// ===== UTF-8 POLISH =====
uint8_t mapUtf8Polish(uint16_t unicode) {
  switch (unicode) {
    case 0x0105:     return 0xB8;  // ą
    case 0x0107:  return 0xBD;  // ć
    case 0x0119:  return 0xD6;  // ę
    case 0x0142:  return 0xCF;  // ł
    case 0x0144:  return 0xC0;  // ń
    case 0x00F3:  return 0xBE;  // ó
    case 0x015B:   return 0xCB;  // ś
    case 0x017A:  return 0xBB;  // ź
    case 0x017C:  return 0xB9;  // ż
    case 0x0104:  return 0xB7;  // Ą
    case 0x0106: return 0xC4;  // Ć
    case 0x0118: return 0x90;  // Ę
    case 0x0141: return 0xD0;  // Ł
    case 0x0143: return 0xC1;  // Ń
    case 0x00D3: return 0xBF;  // Ó
    case 0x015A: return 0xCC;  // Ś
    case 0x0179: return 0xBC;  // Ź
    case 0x017B: return 0xBA;  // Ż
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

// Funkcja rysowania liczby używając custom czcionki 5x7
void drawNumber5x7(int16_t x, int16_t y, int number, uint8_t scale = 1, uint16_t color = SSD1306_WHITE) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", number);
  drawString5x7(x, y, buf, scale, color);
}

// ===== BATTERY MEASUREMENT =====
int readBatteryPercent() {
  // Konfiguracja ADC
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(BATTERY_ADC_PIN, ADC_ATTEN_DB_11);
  
  // Kalibracja ADC
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  
  // Wielokrotny pomiar dla dokładności
  uint32_t voltage = 0;
  for (int i = 0; i < 10; i++) {
    voltage += esp_adc_cal_raw_to_voltage(adc1_get_raw(BATTERY_ADC_PIN), &adc_chars);
    delay(10);
  }
  voltage /= 10;
  
  // Uwzględnij dzielnik napięcia jeśli jest używany
  voltage = (uint32_t)(voltage * BATTERY_DIVIDER_RATIO);
  
  DPRINT("Battery voltage: ");
  DPRINT(voltage);
  DPRINTLN(" mV");
  
  // Mapuj napięcie na procenty (3.0V - 4.2V)
  if (voltage >= BATTERY_MAX_MV) return 100;
  if (voltage <= BATTERY_MIN_MV) return 0;
  
  int percent = map(voltage, BATTERY_MIN_MV, BATTERY_MAX_MV, 0, 100);
  return constrain(percent, 0, 100);
}

// ===== SCROLL FUNCTIONS =====
void prepareScroll(int line, const char* txt, int scale) {
  int singleWidth = getPixelWidth5x7(txt, scale);
  int availWidth = scrollConfs[line].width;

  bool needsScroll = singleWidth > availWidth;

  scrollStates[line].singleTextWidth = singleWidth;
  scrollStates[line].pos = 0;
  scrollStates[line].t_start = millis();
  scrollStates[line].t_last = millis();
  scrollStates[line].isMoving = false;

  if (needsScroll) {
    int suffixWidth = getPixelWidth5x7(scrollSuffix, scale);
    // Buduj text z char[] zamiast String
    snprintf(scrollStates[line].text, sizeof(scrollStates[line].text), 
             "%s%s%s", txt, scrollSuffix, txt);
    scrollStates[line].suffixWidth = suffixWidth;
    scrollStates[line].scrolling = true;
  } else {
    strncpy(scrollStates[line].text, txt, sizeof(scrollStates[line].text) - 1);
    scrollStates[line].text[sizeof(scrollStates[line].text) - 1] = '\0';
    scrollStates[line].suffixWidth = 0;
    scrollStates[line].scrolling = false;
  }
}

void updateScroll(int line) {
  unsigned long now = millis();
  auto& conf = scrollConfs[line];
  auto& state = scrollStates[line];

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

void drawRssiBottom() {
  const int yLine = 52;
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

void updateDisplay() {
  // Poprawiony throttling (fix overflow issue)
  if ((millis() - lastDisplayUpdate) < DISPLAY_REFRESH_RATE_MS) {
    return;
  }
  lastDisplayUpdate = millis();

  display.clearDisplay();

  if (volumeChanging && ((millis() - volumeChangeTime) >= VOLUME_DISPLAY_TIME)) {
    volumeChanging = false;
  }

  // VOLUME SCREEN
  if (volumeChanging) {
    display.fillRect(0, 0, SCREEN_WIDTH, 16, SSD1306_WHITE);
    const char* headerText = "GŁOŚNOŚĆ";
    int headerWidth = getPixelWidth5x7(headerText, 2);
    int headerX = (SCREEN_WIDTH - headerWidth) / 2;
    drawString5x7(headerX, 1, headerText, 2, SSD1306_BLACK);

    char volText[4];
    snprintf(volText, sizeof(volText), "%d", volume);
    int volTextWidth = getPixelWidth5x7(volText, 3);
    int startX = (SCREEN_WIDTH - volTextWidth) / 2;
    int centerY = 25;

    drawString5x7(startX, centerY, volText, 3, SSD1306_WHITE);

    char ipText[24];
    IPAddress ip = WiFi.localIP();
    snprintf(ipText, sizeof(ipText), "IP:%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
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

    int ssidWidth = getPixelWidth5x7(WIFI_SSID, 1);
    int ssidX = (SCREEN_WIDTH - ssidWidth) / 2;
    int ssidY = 35;

    drawString5x7(ssidX, ssidY, WIFI_SSID, 1, SSD1306_WHITE);

    char versionText[8];
    snprintf(versionText, sizeof(versionText), "v%s", FIRMWARE_VERSION);
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

    const char* errorText = "Brak WiFi";
    int errorWidth = getPixelWidth5x7(errorText, 1);
    int errorX = (SCREEN_WIDTH - errorWidth) / 2;
    int errorY = 30;
    drawString5x7(errorX, errorY, errorText, 1, SSD1306_WHITE);

    display.display();
    return;
  }

  // MAIN SCREEN
  const int16_t lineHeight = 16;
  display.fillRect(0, 0, SCREEN_WIDTH, lineHeight, SSD1306_WHITE);

  // Synchronizacja dostępu do zmiennych WebSocket (fix race condition)
  portENTER_CRITICAL(&wsMux);
  bool oldShowCreatorLine = showCreatorLine;
  
  if (strcmp(stacja, prev_stacja) != 0) {
    strncpy(prev_stacja, stacja, sizeof(prev_stacja) - 1);
    prev_stacja[sizeof(prev_stacja) - 1] = '\0';
    portEXIT_CRITICAL(&wsMux);
    prepareScroll(0, stacja, scrollConfs[0].fontsize);
    portENTER_CRITICAL(&wsMux);
  }

  if (wykonawca[0] == '\0') {
    if (strcmp(utwor, prev_utwor) != 0 || oldShowCreatorLine != false) {
      strncpy(prev_utwor, utwor, sizeof(prev_utwor) - 1);
      prev_utwor[sizeof(prev_utwor) - 1] = '\0';
      portEXIT_CRITICAL(&wsMux);
      prepareScroll(1, utwor, scrollConfs[1].fontsize);
      portENTER_CRITICAL(&wsMux);
    }
    showCreatorLine = false;
  } else {
    if (strcmp(wykonawca, prev_wykonawca) != 0 || oldShowCreatorLine != true) {
      strncpy(prev_wykonawca, wykonawca, sizeof(prev_wykonawca) - 1);
      prev_wykonawca[sizeof(prev_wykonawca) - 1] = '\0';
      portEXIT_CRITICAL(&wsMux);
      prepareScroll(1, wykonawca, scrollConfs[1].fontsize);
      portENTER_CRITICAL(&wsMux);
    }

    if (strcmp(utwor, prev_utwor) != 0 || oldShowCreatorLine != true) {
      strncpy(prev_utwor, utwor, sizeof(prev_utwor) - 1);
      prev_utwor[sizeof(prev_utwor) - 1] = '\0';
      portEXIT_CRITICAL(&wsMux);
      prepareScroll(2, utwor, scrollConfs[2].fontsize);
      portENTER_CRITICAL(&wsMux);
    }
    showCreatorLine = true;
  }
  portEXIT_CRITICAL(&wsMux);

  if (oldShowCreatorLine != showCreatorLine) {
    activeScrollLine = 0;
    unsigned long now = millis();
    scrollStates[0].t_start = now;
    scrollStates[0].t_last = now;
    scrollStates[0].pos = 0;
    scrollStates[0].isMoving = false;
    
    scrollStates[1].t_start = now;
    scrollStates[1].t_last = now;
    scrollStates[1].pos = 0;
    scrollStates[1].isMoving = false;
    
    if (showCreatorLine) {
      scrollStates[2].t_start = now;
      scrollStates[2].t_last = now;
      scrollStates[2].pos = 0;
      scrollStates[2].isMoving = false;
    }
  } else {
    if (! showCreatorLine && activeScrollLine > 1) {
      activeScrollLine = 0;
    }
  }

  updateScroll(0);
  if (showCreatorLine) {
    updateScroll(1);
    updateScroll(2);
  } else {
    updateScroll(1);
  }

  drawScrollLine(0, scrollConfs[0].fontsize);
  if (showCreatorLine) {
    drawScrollLine(1, scrollConfs[1].fontsize);
    drawScrollLine(2, scrollConfs[2].fontsize);
  } else {
    drawScrollLine(1, scrollConfs[1].fontsize);
  }

  drawRssiBottom();

  const int yLine = 52;
  int batX = 25;
  int batWidth = 20;
  int batHeight = 8;

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

  int volX = 54;
  display.drawBitmap(volX, yLine, speakerIcon, 8, 8, SSD1306_WHITE);
  
  // Użyj custom czcionki dla volume (zamiast display.print)
  drawNumber5x7(volX + 10, yLine, volume, 1, SSD1306_WHITE);

  portENTER_CRITICAL(&wsMux);
  bool isPlaying = (playerwrap[0] != '\0' &&
                    strcmp(playerwrap, "stop") != 0 &&
                    strcmp(playerwrap, "pause") != 0 &&
                    strcmp(playerwrap, "stopped") != 0 &&
                    strcmp(playerwrap, "paused") != 0);
  int localBitrate = bitrate;
  char localFmt[16];
  strncpy(localFmt, fmt, sizeof(localFmt) - 1);
  localFmt[sizeof(localFmt) - 1] = '\0';
  portEXIT_CRITICAL(&wsMux);

  if (isPlaying) {
    int cursorX = 85;
    
    // Wyświetl bitrate używając custom czcionki
    if (localBitrate > 0) {
      drawNumber5x7(cursorX, yLine, localBitrate, 1, SSD1306_WHITE);
      
      // Oblicz szerokość liczby
      char bitrateStr[6];
      snprintf(bitrateStr, sizeof(bitrateStr), "%d", localBitrate);
      cursorX += getPixelWidth5x7(bitrateStr, 1);
    }
    
    // Wyświetl format
    if (localFmt[0] != '\0') {
      // Dodaj spację tylko gdy bitrate < 1000
      if (localBitrate > 0 && localBitrate < 1000) {
        drawString5x7(cursorX, yLine, " ", 1, SSD1306_WHITE);
        cursorX += 6;  // Szerokość spacji
      }
      drawString5x7(cursorX, yLine, localFmt, 1, SSD1306_WHITE);
    }
  }

  display.display();
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    DPRINTLN("WebSocket connected!");
    portENTER_CRITICAL(&wsMux);
    wsConnected = true;
    lastWebSocketMessage = millis();
    wsReconnectCount = 0;  // Reset licznika reconnect
    portEXIT_CRITICAL(&wsMux);
    webSocket.sendTXT("getindex=1");
    return;
  }

  if (type == WStype_TEXT) {
    portENTER_CRITICAL(&wsMux);
    lastWebSocketMessage = millis();
    portEXIT_CRITICAL(&wsMux);
    
    DPRINT("WebSocket message: ");
    DPRINTLN((char*)payload);

    // Zwiększony rozmiar bufora + sprawdzanie overflow
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    
    if (error) {
      DPRINT("JSON parse error: ");
      DPRINTLN(error.c_str());
      return;
    }
    
    // Sprawdź overflow
    if (doc.overflowed()) {
      DPRINTLN("JSON buffer overflow!  Zwiększ rozmiar StaticJsonDocument");
      return;
    }

    if (doc.containsKey("payload")) {
      JsonArray arr = doc["payload"].as<JsonArray>();
      
      // Synchronizacja dostępu do zmiennych (fix race condition)
      portENTER_CRITICAL(&wsMux);
      
      for (JsonObject obj : arr) {
        const char* id = obj["id"];
        
        if (strcmp(id, "nameset") == 0) {
          const char* value = obj["value"];
          strncpy(stacja, value, sizeof(stacja) - 1);
          stacja[sizeof(stacja) - 1] = '\0';
        }
        else if (strcmp(id, "meta") == 0) {
          const char* metaStr = obj["value"];
          
          // ========== FILTRUJ STATUSY POŁĄCZENIA ==========
          // Zamień [connecting], [łącze], [loading] itp. na [Łączę]
          if (strstr(metaStr, "[connecting]") != nullptr ||
              strstr(metaStr, "[łącze]") != nullptr ||
              strstr(metaStr, "[loading]") != nullptr ||
              strstr(metaStr, "[buffering]") != nullptr) {
            
            // Wyświetl "[Łączę]" zamiast oryginalnego tekstu
            wykonawca[0] = '\0';  // Wyczyść wykonawcę
            strcpy(utwor, "[Łączę]");
    
          } else {
            // ========== NORMALNE PRZETWARZANIE META ==========
            const char* sep = strstr(metaStr, " - ");
            
            if (sep != nullptr) {
              // Format: "Wykonawca - Utwór"
              size_t len = sep - metaStr;
              len = (len < sizeof(wykonawca) - 1) ? len : sizeof(wykonawca) - 1;
              strncpy(wykonawca, metaStr, len);
              wykonawca[len] = '\0';
              
              strncpy(utwor, sep + 3, sizeof(utwor) - 1);
              utwor[sizeof(utwor) - 1] = '\0';
            } else {
              // Brak separatora " - " - tylko tytuł
              wykonawca[0] = '\0';
              strncpy(utwor, metaStr, sizeof(utwor) - 1);
              utwor[sizeof(utwor) - 1] = '\0';
            }
          }
        }
        else if (strcmp(id, "volume") == 0) {
          volume = obj["value"];
        }
        else if (strcmp(id, "bitrate") == 0) {
          if (obj["value"].is<int>()) {
            bitrate = obj["value"];
            DPRINT("DEBUG: bitrate (int) = ");
            DPRINTLN(bitrate);
          } else if (obj["value"].is<const char*>()) {
            const char* bitrateStr = obj["value"];
            DPRINT("DEBUG: bitrate (string) = '");
            DPRINT(bitrateStr);
            DPRINTLN("'");
          }
        }
        else if (strcmp(id, "fmt") == 0) {
          const char* fmtValue = obj["value"];
          if (strcmp(fmtValue, "bitrate") == 0) {
            strcpy(fmt, "kbs");
          } else {
            strncpy(fmt, fmtValue, sizeof(fmt) - 1);
            fmt[sizeof(fmt) - 1] = '\0';
          }
          DPRINT("DEBUG: fmt = '");
          DPRINT(fmt);
          DPRINTLN("'");
        }
        else if (strcmp(id, "playerwrap") == 0) {
          const char* value = obj["value"];
          strncpy(playerwrap, value, sizeof(playerwrap) - 1);
          playerwrap[sizeof(playerwrap) - 1] = '\0';
          DPRINT("DEBUG: playerwrap = '");
          DPRINT(playerwrap);
          DPRINTLN("'");
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

  // Walidacja GPIO dla deep sleep (fix GPIO validation)
  #if BTN_CENTER < 0 || BTN_CENTER > 5
    #error "BTN_CENTER musi być GPIO 0-5 (RTC GPIO) dla ESP32-C3!"
  #endif
  
  // ESP32-C3 Deep Sleep GPIO Wakeup
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
    case WL_NO_SSID_AVAIL:   DPRINTLN("NO SSID AVAILABLE"); break;
    case WL_SCAN_COMPLETED:  DPRINTLN("SCAN COMPLETED"); break;
    case WL_CONNECTED:  DPRINTLN("CONNECTED"); break;
    case WL_CONNECT_FAILED:   DPRINTLN("CONNECT FAILED"); break;
    case WL_CONNECTION_LOST:  DPRINTLN("CONNECTION LOST"); break;
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
    if (! WiFi.config(staticIP, gateway, subnet, dns1, dns2)) {
      DPRINTLN("Static IP config failed!  Falling back to DHCP");
    } else {
      DPRINT("Static IP configured:  ");
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

void handleButtons() {
  // Odczyt AKTUALNYCH stanów przycisków (LOW = wciśnięty)
  bool curUp = (digitalRead(BTN_UP) == LOW);
  bool curDown = (digitalRead(BTN_DOWN) == LOW);
  bool curCenter = (digitalRead(BTN_CENTER) == LOW);
  bool curLeft = (digitalRead(BTN_LEFT) == LOW);
  bool curRight = (digitalRead(BTN_RIGHT) == LOW);

  bool anyButtonPressed = false;

  // ========== VOLUME UP (z auto-repeat) ==========
  if (curUp) {
    if (volumeUpPressTime == 0) {
      // Pierwsze naciśnięcie
      sendCommand("volp=1");
      volumeUpPressTime = millis();
      lastVolumeCommandTime = millis();
      volumeChanging = true;
      volumeChangeTime = millis();
      anyButtonPressed = true;
    } else {
      // Przytrzymanie - auto-repeat
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
    volumeUpPressTime = 0;  // Reset gdy puszczony
  }

  // ========== VOLUME DOWN (z auto-repeat) ==========
  if (curDown) {
    if (volumeDownPressTime == 0) {
      // Pierwsze naciśnięcie
      sendCommand("volm=1");
      volumeDownPressTime = millis();
      lastVolumeCommandTime = millis();
      volumeChanging = true;
      volumeChangeTime = millis();
      anyButtonPressed = true;
    } else {
      // Przytrzymanie - auto-repeat
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
    volumeDownPressTime = 0;  // Reset gdy puszczony
  }

  // ========== CENTER (toggle - z debounce) ==========
  static bool lastCenterState = false;
  if (curCenter && ! lastCenterState) {
    // Zbocze narastające (wciśnięcie)
    sendCommand("toggle=1");
    anyButtonPressed = true;
  }
  lastCenterState = curCenter;

  // ========== LEFT (prev - z debounce) ==========
  static bool lastLeftState = false;
  if (curLeft && !lastLeftState) {
    // Zbocze narastające (wciśnięcie)
    sendCommand("prev=1");
    anyButtonPressed = true;
  }
  lastLeftState = curLeft;

  // ========== RIGHT (next - z debounce) ==========
  static bool lastRightState = false;
  if (curRight && !lastRightState) {
    // Zbocze narastające (wciśnięcie)
    sendCommand("next=1");
    anyButtonPressed = true;
  }
  lastRightState = curRight;

  // Resetuj timer bezczynności
  if (anyButtonPressed) {
    lastActivityTime = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize default scrollStates
  for (int i = 0; i < 3; ++i) {
    scrollStates[i].pos = 0;
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
    case ESP_SLEEP_WAKEUP_GPIO:  DPRINTLN("Wakeup:   GPIO (button pressed)"); break;
    case ESP_SLEEP_WAKEUP_TIMER: DPRINTLN("Wakeup:  TIMER"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: DPRINTLN("Wakeup:  TOUCHPAD"); break;
    case ESP_SLEEP_WAKEUP_ULP: DPRINTLN("Wakeup:  ULP"); break;
    case ESP_SLEEP_WAKEUP_UNDEFINED: DPRINTLN("Wakeup: normal boot / power on"); break;
    default: DPRINTF("Wakeup: %d\n", wakeupReason); break;
  }

  DPRINT("\n\nStarting YoRadio OLED Display v");
  DPRINTLN(FIRMWARE_VERSION);
  DPRINTLN("ESP32-C3 Super Mini");
  DPRINT("YoRadio IP: ");
  DPRINTLN(YORADIO_IP);

  // Pomiar baterii na starcie (fix battery measurement)
  DPRINTLN("=== Battery Measurement ===");
  batteryPercent = readBatteryPercent();
  DPRINT("Battery: ");
  DPRINT(batteryPercent);
  DPRINTLN("%");

  // Watchdog dla ESP32-C3
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

  // Inicjalizacja I2C dla ESP32-C3
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

  // Reset activity time po wakeup (fix deep sleep timeout issue)
  lastActivityTime = millis();
  lastWebSocketMessage = millis();
  lastDisplayUpdate = millis();
}

void loop() {
  esp_task_wdt_reset();

  // ========== PRZYCISKI (najwyższy priorytet) ==========
  static unsigned long lastButtonCheck = 0;
  if ((millis() - lastButtonCheck) >= 20) {  // Co 20ms
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
      DPRINT("IP: ");
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

/*
void loop() {
  esp_task_wdt_reset();

  // ========== PRZYCISKI
  if ((millis() - lastButtonCheck) >= 20) {  // 20ms = bardzo szybka reakcja
    lastButtonCheck = millis();
    handleButtons();  
  }

  webSocket.loop();

  // WiFi state machine
  if (wifiState == WIFI_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      DPRINTLN("WiFi connected!");
      DPRINT("IP: ");
      DPRINTLN(WiFi.localIP());
      DPRINT("Signal: ");
      DPRINT(WiFi.RSSI());
      DPRINTLN(" dBm");
      
      wifiState = WIFI_OK;
      wifiRetryCount = 0;
      
      delay(500);
      
      // WebSocket reconnect limit (fix WebSocket reconnect)
      if (wsReconnectCount < maxWsReconnects) {
        DPRINT("Connecting to WebSocket at ");
        DPRINT(YORADIO_IP);
        DPRINTLN(":80/ws");
        webSocket.begin(YORADIO_IP, 80, "/ws");
        webSocket.onEvent(webSocketEvent);
        webSocket.setReconnectInterval(5000);
        
        wsReconnectCount++;
        lastWsReconnectAttempt = millis();
        lastWebSocketMessage = millis();
      } else {
        DPRINTLN("Max WebSocket reconnects reached");
        // Reset licznika po 5 minutach
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
          DPRINTLN("Retrying...");
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
      DPRINTLN("Retrying from ERROR...");
      wifiRetryCount = 0;
      connectWiFi();
    }
  }

  updateDisplay();

  // if ((millis() - lastButtonCheck) >= 20) {
  //   lastButtonCheck = millis();

  //   // Odczyt stanów przycisków
  //   bool curUp = digitalRead(BTN_UP) == LOW;
  //   bool curRight = digitalRead(BTN_RIGHT) == LOW;
  //   bool curCenter = digitalRead(BTN_CENTER) == LOW;
  //   bool curLeft = digitalRead(BTN_LEFT) == LOW;
  //   bool curDown = digitalRead(BTN_DOWN) == LOW;

  //   // Odczyt poprzednich stanów z bitfield (fix button state optimization)
  //   bool lastCenterState = (lastButtonStates >> BTN_CENTER_BIT) & 1;
  //   bool lastLeftState = (lastButtonStates >> BTN_LEFT_BIT) & 1;
  //   bool lastRightState = (lastButtonStates >> BTN_RIGHT_BIT) & 1;
  //   bool lastUpState = (lastButtonStates >> BTN_UP_BIT) & 1;
  //   bool lastDownState = (lastButtonStates >> BTN_DOWN_BIT) & 1;

  //   bool anyButtonPressed = false;

  //   // Volume UP - z powtarzaniem (fix volume button behavior)
  //   if (curUp) {
  //     if (! lastUpState) {
  //       // Pierwsze naciśnięcie
  //       sendCommand("volp=1");
  //       volumeUpPressTime = millis();
  //       lastVolumeCommandTime = millis();
  //       volumeChanging = true;
  //       volumeChangeTime = millis();
  //       anyButtonPressed = true;
  //     } else {
  //       // Przytrzymanie - powtarzaj polecenia
  //       unsigned long pressDuration = millis() - volumeUpPressTime;
  //       if (pressDuration >= VOLUME_REPEAT_DELAY_MS) {
  //         if ((millis() - lastVolumeCommandTime) >= VOLUME_REPEAT_RATE_MS) {
  //           sendCommand("volp=1");
  //           lastVolumeCommandTime = millis();
  //           volumeChangeTime = millis();
  //           anyButtonPressed = true;
  //         }
  //       }
  //     }
  //   } else {
  //     volumeUpPressTime = 0;
  //   }

  //   // Volume DOWN - z powtarzaniem
  //   if (curDown) {
  //     if (!lastDownState) {
  //       // Pierwsze naciśnięcie
  //       sendCommand("volm=1");
  //       volumeDownPressTime = millis();
  //       lastVolumeCommandTime = millis();
  //       volumeChanging = true;
  //       volumeChangeTime = millis();
  //       anyButtonPressed = true;
  //     } else {
  //       // Przytrzymanie - powtarzaj polecenia
  //       unsigned long pressDuration = millis() - volumeDownPressTime;
  //       if (pressDuration >= VOLUME_REPEAT_DELAY_MS) {
  //         if ((millis() - lastVolumeCommandTime) >= VOLUME_REPEAT_RATE_MS) {
  //           sendCommand("volm=1");
  //           lastVolumeCommandTime = millis();
  //           volumeChangeTime = millis();
  //           anyButtonPressed = true;
  //         }
  //       }
  //     }
  //   } else {
  //     volumeDownPressTime = 0;
  //   }

  //   // CENTER - toggle (debounced)
  //   if (curCenter && !lastCenterState) {
  //     sendCommand("toggle=1");
  //     anyButtonPressed = true;
  //   }

  //   // LEFT - prev (debounced)
  //   if (curLeft && !lastLeftState) {
  //     sendCommand("prev=1");
  //     anyButtonPressed = true;
  //   }

  //   // RIGHT - next (debounced)
  //   if (curRight && !lastRightState) {
  //     sendCommand("next=1");
  //     anyButtonPressed = true;
  //   }

  //   if (anyButtonPressed) {
  //     lastActivityTime = millis();
  //   }

  //   // Zapisz stany do bitfield
  //   lastButtonStates = 0;
  //   if (! curCenter) lastButtonStates |= (1 << BTN_CENTER_BIT);
  //   if (!curLeft) lastButtonStates |= (1 << BTN_LEFT_BIT);
  //   if (!curRight) lastButtonStates |= (1 << BTN_RIGHT_BIT);
  //   if (!curUp) lastButtonStates |= (1 << BTN_UP_BIT);
  //   if (!curDown) lastButtonStates |= (1 << BTN_DOWN_BIT);
  // }

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
