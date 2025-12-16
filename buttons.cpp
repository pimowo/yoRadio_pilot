#include "buttons.h"
#include "network.h"
#include "display.h"

// Button state variables
unsigned long lastButtonCheck = 0;
bool lastCenterState = HIGH;
bool lastLeftState = HIGH;
bool lastRightState = HIGH;
bool lastUpState = HIGH;
bool lastDownState = HIGH;

// CENTER button long-press state for radio switching
unsigned long centerPressStartTime = 0;
bool centerActionExecuted = false;
bool centerPressReleased = true;

// External variables
extern unsigned long lastActivityTime;

void initButtons() {
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
}

void handleButtons() {
  if (millis() - lastButtonCheck <= 120) {
    return;
  }
  
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
