#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>
#include "config.h"

// Button state variables
extern unsigned long lastButtonCheck;
extern bool lastCenterState;
extern bool lastLeftState;
extern bool lastRightState;
extern bool lastUpState;
extern bool lastDownState;

// CENTER button long-press state for radio switching
extern unsigned long centerPressStartTime;
extern bool centerActionExecuted;
extern bool centerPressReleased;

// ===== BUTTON FUNCTIONS =====

/**
 * Initialize button pins
 */
void initButtons();

/**
 * Handle button input and send commands
 * Updates lastActivityTime when buttons are pressed
 */
void handleButtons();

#endif // BUTTONS_H
