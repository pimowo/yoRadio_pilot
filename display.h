#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "types.h"

// Global display variables
extern Adafruit_SSD1306 display;
extern ScrollConfig scrollConfs[3];
extern ScrollState scrollStates[3];
extern int activeScrollLine;
extern String prev_stacja;
extern String prev_wykonawca;
extern String prev_utwor;
extern const char* scrollSuffix;
extern bool showCreatorLine;
extern bool prevShowCreatorLine;

// Display state variables
extern String meta;
extern String stacja;
extern String wykonawca;
extern String utwor;
extern String playerwrap;
extern String fmt;
extern int volume;
extern int bitrate;
extern int rssi;
extern bool volumeChanging;
extern unsigned long volumeChangeTime;
extern unsigned long lastDisplayUpdate;
extern WifiState wifiState;
extern bool wsConnected;
extern unsigned long lastWebSocketMessage;

// ===== DISPLAY FUNCTIONS =====

/**
 * Set OLED contrast level
 * @param c Contrast value (0-255)
 */
void oledSetContrast(uint8_t c);

/**
 * Draw a single 5x7 character
 * @param x X position
 * @param y Y position
 * @param ch Character to draw
 * @param color Color (SSD1306_WHITE or SSD1306_BLACK)
 * @param scale Scale factor (1 = normal, 2 = double size)
 */
void drawChar5x7(int16_t x, int16_t y, uint8_t ch, uint16_t color = SSD1306_WHITE, uint8_t scale = 1);

/**
 * Draw a string using 5x7 font
 * @param x X position
 * @param y Y position
 * @param s String to draw
 * @param scale Scale factor
 * @param color Color
 */
void drawString5x7(int16_t x, int16_t y, const String &s, uint8_t scale = 1, uint16_t color = SSD1306_WHITE);

/**
 * Get pixel width of a string in 5x7 font
 * @param s String to measure
 * @param scale Scale factor
 * @return Width in pixels
 */
int getPixelWidth5x7(const String &s, uint8_t scale = 1);

/**
 * Prepare scroll state for a line
 * @param line Line number (0-2)
 * @param txt Text to scroll
 * @param scale Font scale
 */
void prepareScroll(int line, const String& txt, int scale);

/**
 * Update scroll position for a line
 * @param line Line number (0-2)
 */
void updateScroll(int line);

/**
 * Draw a scrolling line
 * @param line Line number (0-2)
 * @param scale Font scale
 */
void drawScrollLine(int line, int scale);

/**
 * Draw radio number or RSSI at bottom-left
 */
void drawRadioOrRssiBottom();

/**
 * Update the entire display based on current state
 */
void updateDisplay();

/**
 * Display critical battery warning message
 */
void displayCriticalBatteryWarning();

#endif // DISPLAY_H
