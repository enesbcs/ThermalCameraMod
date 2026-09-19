#ifndef OTA_H
#define OTA_H

#include <Arduino.h>
#include <ESP8266WebServer.h>

// Web-based OTA on GET/POST /update
// Also starts ArduinoOTA so the standard espota / Arduino IDE OTA works.
void otaBegin(ESP8266WebServer &server);
void otaLoop();

#endif
