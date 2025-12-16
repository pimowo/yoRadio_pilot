#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <WebSocketsClient.h>
#include "config.h"
#include "types.h"

// Global network variables
extern WebSocketsClient webSocket;
extern WifiState wifiState;
extern bool wsConnected;
extern unsigned long lastWebSocketMessage;
extern unsigned long wifiTimer;
extern RTC_DATA_ATTR int currentRadio;

// ===== NETWORK FUNCTIONS =====

/**
 * WebSocket event handler
 * @param type Event type
 * @param payload Event payload data
 * @param length Length of payload
 */
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);

/**
 * Switch to a different radio
 * @param radioIndex Index of the radio to switch to (0-based)
 */
void switchToRadio(int radioIndex);

/**
 * Send a command to the current radio
 * @param cmd Command string to send
 */
void sendCommand(const char* cmd);

#endif // NETWORK_H
