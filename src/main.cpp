// ===== INCLUDES (na początku) =====
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

// Nowe API dla ADC (ESP32-C3)
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

#include "font5x7.h"

//==================================================================================================
// FIRMWARE
#define FIRMWARE_VERSION "0.6-C3"

// ====================== USTAWIENIA / SETTINGS ======================
// Debug UART messages:   ustaw na 1 aby włączyć diagnostykę po UART, 0 aby wyłączyć
#define DEBUG_UART 1

// Sieć WiFi
#define WIFI_SSID "pimowo"
#define WIFI_PASS "ckH59LRZQzCDQFiUgj"
#define STATIC_IP "192.168.1.111"
#define GATEWAY_IP "192.168.1.1"
#define SUBNET_MASK "255.255.255.0"
#define DNS1_IP "192.168.1.1"
#define DNS2_IP "8.8.8.8"
#define USE_STATIC_IP 1

// yoRadio - pojedyncze radio
#define RADIO_IP "192.168.1.122"

// Uśpienie
#define DEEP_SLEEP_TIMEOUT_SEC 60        // deep sleep podczas odtwarzania
#define DEEP_SLEEP_TIMEOUT_STOPPED_SEC 5 // deep sleep gdy zatrzymany
#define OLED_TIMEOUT_SEC 20              // wyłączenie OLED po bezczynności

// Klawiatura (ESP32-C3 - GPIO 0-21)
#define BTN_UP     2
#define BTN_RIGHT  3
#define BTN_CENTER 4  // Wakeup pin
#define BTN_LEFT   5
#define BTN_DOWN   6

// Wyświetlacz
#define OLED_SDA 8
#define OLED_SCL 9
#define OLED_BRIGHTNESS 10               // 0-15
#define DISPLAY_REFRESH_RATE_MS 100      // 10 FPS (było 50ms/20 FPS)
#define SCROLL_REFRESH_RATE_MS 200       // 5 FPS podczas scrollowania

// Bateria - ADC
#define BATTERY_ADC_PIN 0                // GPIO0 = ADC1_CH0
#define BATTERY_SAMPLES 10               // próbek do uśrednienia
#define BATTERY_READ_INTERVAL_MS 5000    // odczyt co 5s
#define BATTERY_FULL_MV 4200             // 4.2V = 100%
#define BATTERY_EMPTY_MV 3000            // 3.0V = 0%
#define BATTERY_DIVIDER_R1 100000        // 100kΩ (górny)
#define BATTERY_DIVIDER_R2 100000        // 100kΩ (dolny) - dzielnik 1: 2
#define BATTERY_LOW_BLINK_MS 500

adc_oneshot_unit_handle_t adc1_handle;
adc_cali_handle_t adc1_cali_handle = NULL;
bool adc_calibrated = false;

// Watchdog
#define WDT_TIMEOUT 60

// WebSocket
#define WS_RECONNECT_INTERVAL 2000
#define WS_PING_INTERVAL 30000           // ping co 30s (było 15s)
#define WS_PONG_TIMEOUT 3000
#define WS_LOOP_INTERVAL_MS 50           // throttling webSocket. loop()
#define WS_TIMEOUT_MS 10000
#define WS_INITIAL_DATA_TIMEOUT 3000

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

// State variables
int batteryPercent = 100;
int batteryMillivolts = 3700;
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
bool wsDataReceived = false;
unsigned long lastWebSocketMessage = 0;
unsigned long wsConnectTime = 0;

// Timers (non-blocking)
unsigned long lastButtonCheck = 0;
unsigned long lastActivityTime = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastWsLoop = 0;
unsigned long lastBatteryRead = 0;
unsigned long lastWatchdogReset = 0;

// Button states
bool lastCenterState = HIGH;
bool lastLeftState = HIGH;
bool lastRightState = HIGH;
bool lastUpState = HIGH;
bool lastDownState = HIGH;

// Volume display
bool volumeChanging = false;
unsigned long volumeChangeTime = 0;
const unsigned long VOLUME_DISPLAY_TIME = 2000;

// OLED power management
bool oledActive = true;
unsigned long lastOledActivity = 0;

// WiFi state
enum WifiState { WIFI_CONNECTING, WIFI_ERROR, WIFI_OK };
WifiState wifiState = WIFI_CONNECTING;
unsigned long wifiTimer = 0;
const unsigned long wifiTimeout = 15000;
unsigned long wifiRetryTimer = 0;
const unsigned long wifiRetryInterval = 5000;
int wifiRetryCount = 0;
const int maxWifiRetries = 5;

// WebSocket command queue
struct WSCommand {
  String cmd;
  unsigned long sendTime;
};
#define WS_CMD_QUEUE_SIZE 10
WSCommand wsCmdQueue[WS_CMD_QUEUE_SIZE];
int wsCmdQueueHead = 0;
int wsCmdQueueTail = 0;
const unsigned long WS_CMD_INTERVAL_MS = 50; // 50ms między komendami
unsigned long lastWsCmdSent = 0;

// Icons
const unsigned char speakerIcon[] PROGMEM = {
  0b00011000, 0b00111000, 0b11111100, 0b11111100,
  0b11111100, 0b00111000, 0b00011000, 0b00001000
};

const unsigned char wifiErrorIcon[] PROGMEM = {
  0b00011000, 0b00100100, 0b01000010, 0b01000010,
  0b01000010, 0b00100100, 0b00011000, 0b00001000
};

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
int activeScrollLine = 0;

String prev_stacja = "";
String prev_wykonawca = "";
String prev_utwor = "";

const char* scrollSuffix = " * ";

// ===== FUNCTION DECLARATIONS (dla PlatformIO) =====
void drawChar5x7(int16_t x, int16_t y, uint8_t ch, uint16_t color = SSD1306_WHITE, uint8_t scale = 1);
void drawString5x7(int16_t x, int16_t y, const String &s, uint8_t scale = 1, uint16_t color = SSD1306_WHITE);
int getPixelWidth5x7(const String &s, uint8_t scale = 1);
void initBattery();
int readBatteryMillivolts();
void updateBattery();
void queueWSCommand(const String& cmd);
void processWSCommandQueue();
void prepareScroll(int line, const String& txt, int scale);
void updateScroll(int line);
void drawScrollLine(int line, int scale);
void drawRssiBottom();
void updateDisplay();
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void sendCommand(const char* cmd);
void wakeOLED();
void sleepOLED();
void checkOLEDTimeout();
void enterDeepSleep();
void oledSetContrast(uint8_t c);
void connectWiFi();

// ===== UTF-8 POLISH =====
uint8_t mapUtf8Polish(uint16_t unicode) {
  switch (unicode) {
    case 0x0105:  return 0xB8; // ą
    case 0x0107: return 0xBD; // ć
    case 0x0119: return 0xD6; // ę
    case 0x0142: return 0xCF; // ł
    case 0x0144: return 0xC0; // ń
    case 0x00F3: return 0xBE; // ó
    case 0x015B: return 0xCB; // ś
    case 0x017A: return 0xBB; // ź
    case 0x017C: return 0xB9; // ż
    case 0x0104: return 0xB7; // Ą
    case 0x0106: return 0xC4; // Ć
    case 0x0118: return 0x90; // Ę
    case 0x0141: return 0xD0; // Ł
    case 0x0143: return 0xC1; // Ń
    case 0x00D3: return 0xBF; // Ó
    case 0x015A: return 0xCC; // Ś
    case 0x0179: return 0xBC; // Ź
    case 0x017B: return 0xBA; // Ż
    default: return 0;
  }
}

void drawChar5x7(int16_t x, int16_t y, uint8_t ch, uint16_t color, uint8_t scale) {
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

void drawString5x7(int16_t x, int16_t y, const String &s, uint8_t scale, uint16_t color) {
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
        uint8_t c2 = (uint8_t)str[i + 1];
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

int getPixelWidth5x7(const String &s, uint8_t scale) {
  int glyphs = 0;
  const char *str = s.c_str();
  for (size_t i = 0; i < s. length();) {
    uint8_t c = (uint8_t)str[i];
    if (c < 128) { 
      glyphs++; 
      i++; 
    } else if ((c & 0xE0) == 0xC0 && i + 1 < s.length()) { 
      glyphs++; 
      i += 2; 
    } else {
      i++;
    }
  }
  return glyphs * (5 + 1) * scale;
}

// ===== BATTERY ADC =====
esp_adc_cal_characteristics_t adc_chars;

void initBattery() {
  // Konfiguracja ADC oneshot
  adc_oneshot_unit_init_cfg_t init_config = {
    .unit_id = ADC_UNIT_1,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

  // Konfiguracja kanału
  adc_oneshot_chan_cfg_t config = {
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_12,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &config));

  // Kalibracja
  adc_cali_line_fitting_config_t cali_config = {
    .unit_id = ADC_UNIT_1,
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_12,
  };
  
  esp_err_t ret = adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_handle);
  adc_calibrated = (ret == ESP_OK);
  
  DPRINTLN("Battery ADC initialized (GPIO0, divider 1:2)");
}

int readBatteryMillivolts() {
  int adc_raw;
  int voltage = 0;
  int sum = 0;
  
  for (int i = 0; i < BATTERY_SAMPLES; i++) {
    adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &adc_raw);
    sum += adc_raw;
  }
  adc_raw = sum / BATTERY_SAMPLES;
  
  if (adc_calibrated) {
    adc_cali_raw_to_voltage(adc1_cali_handle, adc_raw, &voltage);
  } else {
    voltage = (adc_raw * 3300) / 4095;
  }
  
  voltage *= 2;  // Dzielnik 1:2
  
  DPRINTF("Battery: ADC=%d, mV=%d\n", adc_raw, voltage);
  
  return voltage;
}

void updateBattery() {
  if (millis() - lastBatteryRead < BATTERY_READ_INTERVAL_MS) return;
  lastBatteryRead = millis();
  
  batteryMillivolts = readBatteryMillivolts();
  
  // Mapuj 3. 0V-4.2V → 0-100%
  batteryPercent = map(batteryMillivolts, BATTERY_EMPTY_MV, BATTERY_FULL_MV, 0, 100);
  batteryPercent = constrain(batteryPercent, 0, 100);
  
  DPRINTF("Battery: %dmV = %d%%\n", batteryMillivolts, batteryPercent);
}

// ===== WEBSOCKET COMMAND QUEUE (non-blocking) =====
void queueWSCommand(const String& cmd) {
  int nextHead = (wsCmdQueueHead + 1) % WS_CMD_QUEUE_SIZE;
  if (nextHead == wsCmdQueueTail) {
    DPRINTLN("WS queue full!");
    return;
  }
  
  wsCmdQueue[wsCmdQueueHead].cmd = cmd;
  wsCmdQueue[wsCmdQueueHead].sendTime = millis();
  wsCmdQueueHead = nextHead;
  
  DPRINT("Queued:  ");
  DPRINTLN(cmd);
}

void processWSCommandQueue() {
  if (wsCmdQueueTail == wsCmdQueueHead) return; // empty
  if (! wsConnected) return;
  if (millis() - lastWsCmdSent < WS_CMD_INTERVAL_MS) return; // throttle
  
  String cmd = wsCmdQueue[wsCmdQueueTail].cmd;
  wsCmdQueueTail = (wsCmdQueueTail + 1) % WS_CMD_QUEUE_SIZE;
  
  webSocket.sendTXT(cmd);
  lastWsCmdSent = millis();
  
  DPRINT("Sent: ");
  DPRINTLN(cmd);
}

// ===== SCROLL FUNCTIONS =====
void prepareScroll(int line, const String& txt, int scale) {
  int singleWidth = getPixelWidth5x7(txt, scale);
  int availWidth = scrollConfs[line].width;
  bool needsScroll = singleWidth > availWidth;

  scrollStates[line].singleTextWidth = singleWidth;
  scrollStates[line]. pos = 0;
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

  if (! state.scrolling) {
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
    drawString5x7(x, y, state.text, scale, SSD1306_WHITE);
  }
}

// Draw RSSI bars at bottom-left
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
      display. drawFastHLine(x, yLine + (8 - barHeights[i]), barWidth, SSD1306_WHITE);
  }
}

void updateDisplay() {
  // Jeśli OLED wyłączony, nie rysuj
  if (!oledActive) return;
  
  // Throttle display updates
  unsigned long refreshRate = (scrollStates[activeScrollLine].scrolling && scrollStates[activeScrollLine]. isMoving) 
                              ?  SCROLL_REFRESH_RATE_MS :  DISPLAY_REFRESH_RATE_MS;
  
  if (millis() - lastDisplayUpdate < refreshRate) return;
  lastDisplayUpdate = millis();

  display.clearDisplay();

  // Check if volume screen should be hidden
  if (volumeChanging && (millis() - volumeChangeTime > VOLUME_DISPLAY_TIME)) {
    volumeChanging = false;
  }

  // VOLUME SCREEN
  if (volumeChanging) {
    display.fillRect(0, 0, SCREEN_WIDTH, 16, SSD1306_WHITE);
    String headerText = "GŁOŚNOŚĆ";
    int headerWidth = getPixelWidth5x7(headerText, 2);
    int headerX = (SCREEN_WIDTH - headerWidth) / 2;
    drawString5x7(headerX, 1, headerText, 2, SSD1306_BLACK);

    int volScale = 3;
    String volText = String(volume);
    int volTextWidth = getPixelWidth5x7(volText, volScale);
    int startX = (SCREEN_WIDTH - volTextWidth) / 2;
    int centerY = 25;

    drawString5x7(startX, centerY, volText, volScale, SSD1306_WHITE);

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

  // Check WebSocket status
  bool wsError = ! wsConnected || ((millis() - lastWebSocketMessage) > WS_TIMEOUT_MS);

  if (wsError) {
    String errorText = "Brak yoRadio! ";
    int errorWidth = getPixelWidth5x7(errorText, 1);
    int errorX = (SCREEN_WIDTH - errorWidth) / 2;
    int errorY = 30;
    drawString5x7(errorX, errorY, errorText, 1, SSD1306_WHITE);

    drawRssiBottom();

    display.display();
    return;
  }

  // MAIN SCREEN
  display.fillRect(0, 0, SCREEN_WIDTH, 16, SSD1306_WHITE);

  // Prepare scrolls on change
  bool oldShowCreatorLine = showCreatorLine;

  if (stacja != prev_stacja) {
    prev_stacja = stacja;
    prepareScroll(0, stacja, scrollConfs[0].fontsize);
  }

  if (wykonawca. isEmpty()) {
    if (utwor != prev_utwor || oldShowCreatorLine != false) {
      prev_utwor = utwor;
      prepareScroll(1, utwor, scrollConfs[1].fontsize);
    }
    showCreatorLine = false;
  } else {
    if (wykonawca != prev_wykonawca || oldShowCreatorLine != true) {
      prev_wykonawca = wykonawca;
      prepareScroll(1, wykonawca, scrollConfs[1].fontsize);
    }

    if (utwor != prev_utwor || oldShowCreatorLine != true) {
      prev_utwor = utwor;
      prepareScroll(2, utwor, scrollConfs[2]. fontsize);
    }
    showCreatorLine = true;
  }

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

  // Update scrolls
  updateScroll(0);
  if (showCreatorLine) {
    updateScroll(1);
    updateScroll(2);
  } else {
    updateScroll(1);
  }

  // Draw all lines
  drawScrollLine(0, scrollConfs[0].fontsize);
  if (showCreatorLine) {
    drawScrollLine(1, scrollConfs[1].fontsize);
    drawScrollLine(2, scrollConfs[2].fontsize);
  } else {
    drawScrollLine(1, scrollConfs[1].fontsize);
  }

  // RSSI bars
  drawRssiBottom();

  // BATTERY
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

  // VOLUME
  int volX = 54;
  display.drawBitmap(volX, yLine, speakerIcon, 8, 8, SSD1306_WHITE);
  display.setCursor(volX + 10, yLine);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.print(volume);

  // BITRATE (only when playing)
  bool isPlaying = (! playerwrap.isEmpty() &&
                    playerwrap != "stop" &&
                    playerwrap != "pause" &&
                    playerwrap != "stopped" &&
                    playerwrap != "paused");

  if (isPlaying) {
    display.setCursor(85, yLine);
    if (bitrate > 0) {
      display.print(bitrate);
    }
    if (! fmt.isEmpty()) {
      display.print(fmt);
    }
  }

  display.display();
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    DPRINTLN("WebSocket connected!");
    wsConnected = true;
    wsConnectTime = millis();
    wsDataReceived = false;
    lastWebSocketMessage = millis();
    
    // Send critical commands immediately (like old code for reliable loading)
    webSocket.sendTXT("getindex=1");
    delay(50);
    webSocket.sendTXT("get=nameset");
    delay(50);
    webSocket.sendTXT("get=meta");
    delay(50);
    webSocket.sendTXT("get=volume");
    delay(50);
    webSocket.sendTXT("get=bitrate");
    delay(50);
    webSocket.sendTXT("get=fmt");
    delay(50);
    webSocket.sendTXT("get=playerwrap");
    
    DPRINTLN("Initial data requests sent");
    return;
  }

  if (type == WStype_TEXT) {
    if (! wsDataReceived) {
      unsigned long loadTime = millis() - wsConnectTime;
      DPRINTF("First data received after %lums\n", loadTime);
      wsDataReceived = true;
    }
    
    lastWebSocketMessage = millis();
    DPRINT("WS:  ");
    DPRINTLN((char*)payload);

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) {
      DPRINT("JSON parse error: ");
      DPRINTLN(error.c_str());
      return;
    }

    bool dataChanged = false;

    if (doc. containsKey("payload")) {
      JsonArray arr = doc["payload"].as<JsonArray>();
      for (JsonObject obj : arr) {
        String id = obj["id"].as<String>();
        
        if (id == "nameset") {
          String newStacja = obj["value"].as<String>();
          if (newStacja != stacja) {
            stacja = newStacja;
            dataChanged = true;
          }
        }
        else if (id == "meta") {
          String metaStr = obj["value"].as<String>();
          int sep = metaStr.indexOf(" - ");

          String newWykonawca = "";
          String newUtwor = "";
          
          if (sep > 0) {
            newWykonawca = metaStr.substring(0, sep);
            newUtwor = metaStr.substring(sep + 3);
          } else {
            newUtwor = metaStr;
          }
          
          if (newWykonawca != wykonawca || newUtwor != utwor) {
            wykonawca = newWykonawca;
            utwor = newUtwor;
            dataChanged = true;
          }
        }
        else if (id == "volume") {
          int newVolume = obj["value"].as<int>();
          if (newVolume != volume) {
            volume = newVolume;
            dataChanged = true;
          }
        }
        else if (id == "bitrate") {
          int newBitrate = obj["value"].as<int>();
          if (newBitrate != bitrate) {
            bitrate = newBitrate;
            dataChanged = true;
          }
        }
        else if (id == "fmt") {
          String newFmt = obj["value"].as<String>();
          if (newFmt != fmt) {
            fmt = newFmt;
            dataChanged = true;
          }
        }
        else if (id == "playerwrap") {
          String newPlayerwrap = obj["value"].as<String>();
          if (newPlayerwrap != playerwrap) {
            playerwrap = newPlayerwrap;
            DPRINTF("playerwrap:  '%s'\n", playerwrap. c_str());
            dataChanged = true;
          }
        }
        else if (id == "rssi") {
          int newRssi = obj["value"].as<int>();
          if (newRssi != rssi) {
            rssi = newRssi;
          }
        }
      }
    }

    if (wifiState == WIFI_OK && dataChanged) {
      // Trigger display update on next loop
      lastDisplayUpdate = 0;
    }
  }

  if (type == WStype_DISCONNECTED) {
    DPRINTLN("WebSocket disconnected!");
    wsConnected = false;
    wsDataReceived = false;
  }
  
  if (type == WStype_ERROR) {
    DPRINT("WebSocket error: ");
    DPRINTLN((char*)payload);
  }
}

void sendCommand(const char* cmd) {
  queueWSCommand(String(cmd));
}

void wakeOLED() {
  if (! oledActive) {
    DPRINTLN("Waking OLED");
    display.ssd1306_command(0xAF); // Display ON
    oledActive = true;
  }
  lastOledActivity = millis();
}

void sleepOLED() {
  if (oledActive) {
    DPRINTLN("Sleeping OLED");
    display.clearDisplay();
    display.display();
    display.ssd1306_command(0xAE); // Display OFF
    oledActive = false;
  }
}

void checkOLEDTimeout() {
  if (!oledActive) return;
  
  unsigned long inactivity = millis() - lastOledActivity;
  if (inactivity > (OLED_TIMEOUT_SEC * 1000)) {
    sleepOLED();
  }
}

void enterDeepSleep() {
  DPRINTLN("Preparing deep sleep...");
  
  sleepOLED();
  webSocket.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  
  // ESP32-C3: GPIO wakeup with proper configuration
  DPRINTLN("Configuring GPIO wakeup on GPIO4 (CENTER, LOW)...");
  gpio_set_direction((gpio_num_t)BTN_CENTER, GPIO_MODE_INPUT);
  gpio_set_pull_mode((gpio_num_t)BTN_CENTER, GPIO_PULLUP_ONLY);
  gpio_wakeup_enable((gpio_num_t)BTN_CENTER, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  
  DPRINTLN("Entering deep sleep...");
  Serial.flush();
  
  esp_deep_sleep_start();
}

void oledSetContrast(uint8_t c) {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(c);
}

void connectWiFi() {
  DPRINTLN("Attempting WiFi connection...");
  
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setScanMethod(WIFI_FAST_SCAN);
  
  // WiFi Power Saving (oszczędność ~20-40mA)
  WiFi.setSleep(WIFI_PS_MIN_MODEM); // min modem sleep
  
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
    DPRINTF("Static IP configured: %s\n", STATIC_IP);
  }
#else
  DPRINTLN("Using DHCP");
#endif
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiTimer = millis();
  wifiRetryTimer = millis();
  wifiState = WIFI_CONNECTING;
  
  DPRINTF("Connecting to:  %s\n", WIFI_SSID);
}

void setup() {
  // ===== CPU FREQUENCY OPTIMIZATION =====
  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  
  #ifdef CONFIG_BT_ENABLED
    btStop();
  #endif
  
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  switch (wakeupReason) {
    case ESP_SLEEP_WAKEUP_GPIO:  DPRINTLN("Wakeup:  GPIO (CENTER button)"); break;
    case ESP_SLEEP_WAKEUP_UNDEFINED: DPRINTLN("Wakeup: Normal boot"); break;
    default: DPRINTF("Wakeup: %d\n", wakeupReason); break;
  }

  DPRINT("\n\nYoRadio OLED Remote v");
  DPRINTLN(FIRMWARE_VERSION);
  DPRINTF("ESP32-C3 @ %luMHz\n", (unsigned long)getCpuFrequencyMhz());

  // Watchdog - nowe API
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  DPRINTLN("Watchdog initialized (60s)");

  // Buttons
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  // Battery ADC
  initBattery();
  updateBattery(); // Initial read

  // I2C for OLED (ESP32-C3 custom pins)
  Wire.begin(OLED_SDA, OLED_SCL);

  // OLED Init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    DPRINTLN("SSD1306 failed!");
    for(;;);
  }

  uint8_t brightness = constrain(OLED_BRIGHTNESS, 0, 15);
  oledSetContrast(brightness * 16);
  DPRINTF("OLED brightness: %d\n", brightness);

  display.clearDisplay();
  display.display();

  // Initialize scroll states
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

  // WiFi
  DPRINTLN("=== WiFi Initialization ===");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(20); // Minimalne opóźnienie
  
  connectWiFi();

  // Timers
  lastActivityTime = millis();
  lastOledActivity = millis();
  lastWebSocketMessage = millis();  // Initialize to prevent immediate timeout
  lastDisplayUpdate = 0;
  wsConnectTime = millis();
  lastWatchdogReset = millis();
}

void loop() {
  // Watchdog reset (co sekundę zamiast co iterację)
  if (millis() - lastWatchdogReset > 1000) {
    esp_task_wdt_reset();
    lastWatchdogReset = millis();
  }

  // WebSocket loop (throttled to 50ms)
  if (millis() - lastWsLoop > WS_LOOP_INTERVAL_MS) {
    webSocket.loop();
    lastWsLoop = millis();
  }

  // Process WebSocket command queue
  processWSCommandQueue();

  // Update display
  updateDisplay();

  // Update battery
  updateBattery();

  // Check OLED timeout
  checkOLEDTimeout();

  // ===== WiFi State Machine =====
  if (wifiState == WIFI_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      DPRINTLN("WiFi connected!");
      DPRINTF("IP: %s\n", WiFi.localIP().toString().c_str());
      DPRINTF("Signal: %d dBm\n", WiFi. RSSI());
      
      wifiState = WIFI_OK;
      wifiRetryCount = 0;
      
      DPRINTF("Connecting to WebSocket at %s: 80/ws\n", RADIO_IP);
      
      wsConnectTime = millis();
      webSocket.begin(RADIO_IP, 80, "/ws");
      webSocket.onEvent(webSocketEvent);
      webSocket.setReconnectInterval(WS_RECONNECT_INTERVAL);
      webSocket.enableHeartbeat(WS_PING_INTERVAL, WS_PONG_TIMEOUT, 2);
      
      DPRINTLN("WebSocket initialized");
    } 
    else {
      if (millis() - wifiTimer > wifiTimeout) {
        wifiRetryCount++;
        DPRINTF("WiFi attempt %d/%d failed\n", wifiRetryCount, maxWifiRetries);
        
        if (wifiRetryCount >= maxWifiRetries) {
          DPRINTLN("Max WiFi retries - ERROR state");
          wifiState = WIFI_ERROR;
          wifiRetryTimer = millis();
        } else {
          DPRINTLN("Retrying.. .");
          WiFi.disconnect();
          delay(20);
          connectWiFi();
        }
      }
    }
  } 
  else if (wifiState == WIFI_OK) {
    if (WiFi.status() != WL_CONNECTED) {
      DPRINTLN("WiFi disconnected!");
      webSocket.disconnect();
      wsConnected = false;
      wsDataReceived = false;
      wifiRetryCount = 0;
      connectWiFi();
    }
  } 
  else if (wifiState == WIFI_ERROR) {
    if (millis() - wifiRetryTimer > wifiRetryInterval) {
      DPRINTLN("Retrying WiFi from ERROR state");
      wifiRetryCount = 0;
      connectWiFi();
    }
  }

  // ===== BUTTON HANDLING (non-blocking) =====
  if (millis() - lastButtonCheck > 120) {
    lastButtonCheck = millis();

    bool curUp = digitalRead(BTN_UP);
    bool curRight = digitalRead(BTN_RIGHT);
    bool curCenter = digitalRead(BTN_CENTER);
    bool curLeft = digitalRead(BTN_LEFT);
    bool curDown = digitalRead(BTN_DOWN);

    bool anyButtonPressed = false;

    // Any button wakes OLED
    if (curUp == LOW || curRight == LOW || curCenter == LOW || curLeft == LOW || curDown == LOW) {
      wakeOLED();
    }

    // UP - next station (edge-triggered, single press)
    if (curUp == LOW && lastUpState == HIGH) {
      sendCommand("next=1");
      anyButtonPressed = true;
    }

    // DOWN - previous station (edge-triggered, single press)
    if (curDown == LOW && lastDownState == HIGH) {
      sendCommand("prev=1");
      anyButtonPressed = true;
    }

    // CENTER - toggle play/pause (edge-triggered)
    if (curCenter == LOW && lastCenterState == HIGH) {
      sendCommand("toggle=1");
      anyButtonPressed = true;
    }

    // LEFT - volume down (continuous while held)
    if (curLeft == LOW) {
      sendCommand("volm=1");
      volumeChanging = true;
      volumeChangeTime = millis();
      lastDisplayUpdate = 0;  // Force immediate display update
      anyButtonPressed = true;
    }

    // RIGHT - volume up (continuous while held)
    if (curRight == LOW) {
      sendCommand("volp=1");
      volumeChanging = true;
      volumeChangeTime = millis();
      lastDisplayUpdate = 0;  // Force immediate display update
      anyButtonPressed = true;
    }

    if (anyButtonPressed) {
      lastActivityTime = millis();
    }

    lastCenterState = curCenter;
    lastLeftState = curLeft;
    lastRightState = curRight;
    lastUpState = curUp;
    lastDownState = curDown;
  }

  // ===== DEEP SLEEP CHECK =====
  unsigned long inactivityTime = millis() - lastActivityTime;

  bool playerStopped = (! playerwrap.isEmpty() &&
                       (playerwrap == "stop" ||
                       playerwrap == "pause" ||
                       playerwrap == "stopped" ||
                       playerwrap == "paused"));
  
  unsigned long timeoutMs = playerStopped 
    ? (DEEP_SLEEP_TIMEOUT_STOPPED_SEC * 1000) 
    : (DEEP_SLEEP_TIMEOUT_SEC * 1000);

  if (inactivityTime > timeoutMs) {
    if (playerStopped) {
      DPRINTLN("Deep sleep:  Player stopped timeout");
    } else {
      DPRINTLN("Deep sleep:  Inactivity timeout");
    }
    enterDeepSleep();
  }

  // Opcjonalne:  małe opóźnienie dla automatic light sleep
  // delay(10);
}
