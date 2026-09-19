#include "wificonf.h"

#include <EEPROM.h>

#define CFG_ADDR 0
#define CFG_SIZE 128
#define CFG_MAGIC 0x5443

bool wifiLoadCfg(WifiCfg &cfg) {
  EEPROM.begin(CFG_SIZE);
  EEPROM.get(CFG_ADDR, cfg);
  EEPROM.end();
  // Force NUL termination so %s formatting can never run off the arrays even
  // if EEPROM contains garbage whose magic accidentally matches.
  cfg.ssid[sizeof(cfg.ssid) - 1] = '\0';
  cfg.pass[sizeof(cfg.pass) - 1] = '\0';
  return cfg.magic == CFG_MAGIC;
}

void wifiSaveCfg(const char *ssid, const char *pass) {
  WifiCfg cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CFG_MAGIC;
  strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
  if (pass) strncpy(cfg.pass, pass, sizeof(cfg.pass) - 1);
  EEPROM.begin(CFG_SIZE);
  EEPROM.put(CFG_ADDR, cfg);
  EEPROM.commit();
  EEPROM.end();
}

void wifiClearCfg() {
  WifiCfg cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = 0;
  EEPROM.begin(CFG_SIZE);
  EEPROM.put(CFG_ADDR, cfg);
  EEPROM.commit();
  EEPROM.end();
}