#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Web server instance
extern AsyncWebServer webServer;

// Function declarations
void setupWebServer();
void startAPMode();
bool isAPMode();

#endif // WEBSERVER_H
