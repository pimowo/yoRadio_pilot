// yoRadio Pilot - ESP32-S3 Remote Control for yoRadio
// Refactored modular architecture v0.5.0

#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include <Wire.h>

// Project headers
#include "config.h"
#include "types.h"
#include "display.h"
#include "network.h"
#include "battery.h"
#include "buttons.h"

// ===== GLOBAL VARIABLES =====
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Activity tracking
unsigned long lastActivityTime = 0;

// ===== DEEP SLEEP FUNCTION =====
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

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(200);  // UART stabilization delay

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

  // currentRadio persists through deep sleep (stored in RTC memory with RTC_DATA_ATTR)
  // Only reset on invalid values, not on every boot
  if (currentRadio < 0 || currentRadio >= NUM_RADIOS) {
    DPRINT("Invalid currentRadio from RTC: ");
    DPRINTLN(currentRadio);
    DPRINTLN("Resetting to default radio 0");
    currentRadio = 0;
  } else {
    DPRINTLN("currentRadio preserved from RTC memory");
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

  // Initialize buttons
  initButtons();

  // ===== BATERIA - INICJALIZACJA ADC =====
  pinMode(BATTERY_PIN, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  analogRead(BATTERY_PIN);  // Dummy read
  delay(10);

  // Pierwszy pomiar (przed WiFi - mniej szumu)
  lastBatteryVoltage = readBatteryVoltage();
  batteryPercent = voltageToPercent(lastBatteryVoltage);
  checkBatteryAndShutdown(lastBatteryVoltage);
  DPRINTF("Napięcie baterii: %.2fV (%d%%)\n", lastBatteryVoltage, batteryPercent);

  // === WYŁĄCZ WS2812 LED ===
  strip.begin();
  strip.clear();
  strip.show();

  // Initialize I2C
  Wire.begin(I2C_SDA, I2C_SCL);

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

  delay(100);  // Stabilization delay before WiFi.begin()
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  // Explicit connection wait loop (max 15 seconds)
  int attempts = 0;
  const int maxAttempts = 30;  // 30 * 500ms = 15 seconds
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    DPRINT(".");
    attempts++;
  }
  DPRINTLN("");
  
  if (WiFi.status() == WL_CONNECTED) {
    DPRINTLN("WiFi connected successfully!");
    DPRINT("IP address: ");
    DPRINTLN(WiFi.localIP());
    wifiState = WIFI_OK;
  } else {
    DPRINTLN("WiFi connection failed!");
    wifiState = WIFI_ERROR;
  }
  
  wifiTimer = millis();

  // Initialize WebSocket if WiFi connected successfully
  if (wifiState == WIFI_OK) {
    DPRINT("Connecting to WebSocket at ");
    DPRINT(RADIO_IPS[currentRadio]);
    DPRINTLN(":80/ws");
    webSocket.begin(RADIO_IPS[currentRadio], 80, "/ws");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(WS_RECONNECT_INTERVAL_MS);  // Faster retry: 3000ms instead of default 5000ms
  }

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

// ===== MAIN LOOP =====
void loop() {
  // Reset watchdog co iterację
  esp_task_wdt_reset();

  // Obsługa OTA updates
  ArduinoOTA.handle();

  webSocket.loop();

  // ===== BATERIA - MONITORING CO 10 SEKUND =====
  static unsigned long lastBatteryCheck = 0;
  if (millis() - lastBatteryCheck >= BATTERY_CHECK_INTERVAL_MS) {
    lastBatteryCheck = millis();
    lastBatteryVoltage = readBatteryVoltage();
    batteryPercent = voltageToPercent(lastBatteryVoltage);
    checkBatteryAndShutdown(lastBatteryVoltage);
    DPRINTF("Napięcie baterii: %.2fV (%d%%)\n", lastBatteryVoltage, batteryPercent);
    esp_task_wdt_reset();
  }

  // WiFi connection management
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
      webSocket.setReconnectInterval(WS_RECONNECT_INTERVAL_MS);  // Faster retry: 3000ms instead of default 5000ms
    }
    if (millis() - wifiTimer > 8000) {
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

  // Handle button input
  handleButtons();

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
