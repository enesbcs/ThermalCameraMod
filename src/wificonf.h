#ifndef WIFICONF_H
#define WIFICONF_H

#include <Arduino.h>

// Persistent STA connection settings (stored in EEPROM).
struct WifiCfg {
  uint16_t magic;     // validity marker
  char ssid[33];
  char pass[65];
};

// Load the stored STA config. Returns false when nothing valid was saved.
bool wifiLoadCfg(WifiCfg &cfg);

// Save STA credentials to EEPROM and commit. pass may be empty (open network).
void wifiSaveCfg(const char *ssid, const char *pass);

// Invalidate the stored config so the device boots into AP mode only.
void wifiClearCfg();

#endif