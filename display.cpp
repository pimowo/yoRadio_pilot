#include "display.h"
#include "font5x7.h"
#include <WiFi.h>

// Global display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Scroll configuration
const ScrollConfig scrollConfs[3] = {
  {2, 1, 2, SCREEN_WIDTH - 4, 10, 2, 1500},
  {0, 19, 2, SCREEN_WIDTH - 2, 10, 2, 1500},
  {0, 38, 1, SCREEN_WIDTH - 2, 10, 2, 1500}
};

// Scroll state
ScrollState scrollStates[3];
int activeScrollLine = 0;
String prev_stacja = "";
String prev_wykonawca = "";
String prev_utwor = "";
const char* scrollSuffix = " * ";
bool showCreatorLine = true;
bool prevShowCreatorLine = true;

// Display state variables
String meta = "";
String stacja = "";
String wykonawca = "";
String utwor = "";
String playerwrap = "";
String fmt = "";
int volume = 0;
int bitrate = 0;
int rssi = 0;
bool volumeChanging = false;
unsigned long volumeChangeTime = 0;
unsigned long lastDisplayUpdate = 0;

// Icons
const unsigned char speakerIcon [] PROGMEM = {
  0b00011000, 0b00111000, 0b11111100, 0b11111100,
  0b11111100, 0b00111000, 0b00011000, 0b00001000
};

const unsigned char wifiErrorIcon[] PROGMEM = {
  0b00011000, 0b00100100, 0b01000010, 0b01000010,
  0b01000010, 0b00100100, 0b00011000, 0b00001000
};

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

int getPixelWidth5x7(const String &s, uint8_t scale) {
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

void drawRadioOrRssiBottom() {
  const int yLine = 52;
  if (NUM_RADIOS > 1) {
    extern int currentRadio;
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
  extern int batteryPercent;
  extern WifiState wifiState;
  extern bool wsConnected;
  extern unsigned long lastWebSocketMessage;
  
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

void displayCriticalBatteryWarning() {
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_WIDTH, 16, SSD1306_WHITE);
  
  String line1 = "BATERIA PUSTA!";
  String line2 = "Naładuj baterię";
  
  int line1Width = getPixelWidth5x7(line1, 1);
  int line2Width = getPixelWidth5x7(line2, 1);
  int line1X = (SCREEN_WIDTH - line1Width) / 2;
  int line2X = (SCREEN_WIDTH - line2Width) / 2;
  
  drawString5x7(line1X, 1, line1, 1, SSD1306_BLACK);
  drawString5x7(line2X, 30, line2, 1, SSD1306_WHITE);
  
  display.display();
  delay(3000);
}

void oledSetContrast(uint8_t c) {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(c);
}
