#ifndef WEB_H
#define WEB_H

#include <Arduino.h>
#include <ESP8266WebServer.h>

// Start the web server. Tries STA first if a config was saved; otherwise it
// falls back to AP mode with captive DNS. Returns true when in AP mode.
bool webBegin();

// Handle client requests + (AP mode only) captive DNS in the main loop.
void webLoop();

#endif