#include "ota.h"

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <Updater.h>

// Minimal web UI for firmware upload
// GET  /update -> form
// POST /update -> stream the .bin into the update partition
static const char UPDATE_PAGE[] PROGMEM =
  "<!DOCTYPE html><html><head><meta name=viewport "
  "content=\"width=device-width,initial-scale=1\">"
  "<title>ThermoCam :: OTA</title>"
  "<style>body{font-family:sans-serif;background:#f2f2f2;color:#333}"
  "nav{background:#0066ff;color:#fff;padding:1em;font-size:1.3em}"
  "a{color:#fff;text-decoration:none}"
  "input{margin:0.5em 0}</style></head>"
  "<body><nav><a href=\"/\">&#8592; ThermoCam</a></nav>"
  "<article><h2>Firmware update</h2>"
  "<form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\">"
  "<input type=\"file\" name=\"update\" accept=\".bin\">"
  "<input type=\"submit\" value=\"Update\">"
  "</form><p><small>Upload the .bin build of the firmware.</small></p>"
  "</article></body></html>";

static ESP8266WebServer *otaServer = nullptr;

static void handleUpdateGet() {
  if (!otaServer) return;
  otaServer->sendHeader("Connection", "close");
  otaServer->send(200, "text/html", FPSTR(UPDATE_PAGE));
}

static void handleUpdatePost() {
  if (!otaServer) return;
  HTTPUpload &upload = otaServer->upload();

  if (upload.status == UPLOAD_FILE_START) {
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace, U_FLASH)) {
      // Not having the space (or the updater) makes further writes pointless;
      // abort and report instead of silently flashing nothing.
      Update.end(false);
      otaServer->send(500, "text/plain", "No space for update");
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.end(false);
      otaServer->send(500, "text/plain", "Flash write error");
      return;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      otaServer->send(200, "text/html",
                      "<meta http-equiv=refresh content=3;url=/>"
                      "<h2>Update OK, rebooting...</h2>");
      delay(500);
      ESP.restart();
    } else {
      otaServer->send(500, "text/plain", "Update failed");
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end(false); // abort without flashing
  }
}

void otaBegin(ESP8266WebServer &server) {
  otaServer = &server;

  // Serve the form
  server.on("/update", HTTP_GET, handleUpdateGet);
  // Receive the upload
  server.on(
      "/update", HTTP_POST,
      []() {
        if (otaServer) {
          otaServer->sendHeader("Connection", "close");
          otaServer->send(200, "text/plain",
                          (Update.hasError()) ? "FAIL" : "OK");
        }
      },
      handleUpdatePost);

  // ArduinoOTA (UDP) handler, used by "platformio run -t upload
  // --upload_protocol espota". Handles the real flashing.
  ArduinoOTA.onStart([]() {});
  ArduinoOTA.onEnd([]() {});
  ArduinoOTA.onProgress([](unsigned int prog, unsigned int total) {});
  ArduinoOTA.onError([](ota_error_t err) { ESP.restart(); });
  ArduinoOTA.begin();
}

void otaLoop() { ArduinoOTA.handle(); }
