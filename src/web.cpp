#include "web.h"
#include "main.h"
#include "config.h"
#include "ota.h"
#include "wificonf.h"

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <JPEGENC.h>

static DNSServer dnsServer;
static ESP8266WebServer webServer(80);
static IPAddress APIP(172, 217, 28, 1);
static bool apMode = true;

// JPEG encoder state (kept off the heap, reused between snapshots)
static JPEGENC jpeg;
#define JPEG_BUF_SIZE 4096

// ---------------------------------------------------------------------------
// Web page fragments (PROGMEM so they are not rebuilt on the heap)
// ---------------------------------------------------------------------------
static const char PAGE_HEAD[] PROGMEM =
    "<!DOCTYPE html><html><head><title>" APSSID " :: " SUBTITLE "</title>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<style>"
    "article{background:#f2f2f2;padding:1.3em;}"
    "body{color:#333;font-family:Century Gothic,sans-serif;font-size:18px;"
    "line-height:24px;margin:0;padding:0;}"
    "div{padding:0.35em;}"
    "h1{margin:0.5em 0 0 0;padding:0.5em;}"
    "nav{background:#0066ff;color:#fff;display:block;font-size:1.3em;padding:1em;}"
    "nav b{display:block;font-size:1.5em;margin-bottom:0.5em;}"
    "input,button{font-size:18px;padding:0.3em;margin:0.2em;}"
    ".footer{position:fixed;left:0;bottom:0;width:100%;background:#0066ff;"
    "color:white;text-align:center;font-family:Verdana,Sans,serif;height:25px;"
    "font-size:14px;}"
    "</style></head>"
    "<body><nav><b>" APSSID "</b> " SUBTITLE "</nav><br>"
    "<center><table><tr><th>AMG8833 sensor: ";

static const char PAGE_IMG[] PROGMEM =
    "online</th></tr><tr><th>Live image<br>"
    "<img id=\"thermal\" src=\"" SNAPSHOT_PATH
    "\" width=\"168\" height=\"168\"><br>"
    "<button onclick=\"dlBigJpg()\">Download (126x126)</button>"
    "<label>&nbsp;<input type=checkbox id=livecyc onchange=\"setLive(this)\">"
    " Live refresh (max 4/s)</label></th></tr>";

static const char PAGE_FOOT_JS[] PROGMEM =
    "</table></center><br>"
    "<div class=\"footer\"><b>Author:</b> NS Tech <b>2026</b></div>"
    "<script>"
    "function dlBigJpg(){"
    "var img=new Image();"
    "img.onload=function(){"
    "var c=document.createElement('canvas'),s=126;"
    "c.width=s;c.height=s;var x=c.getContext('2d');"
    "x.imageSmoothingEnabled=false;x.drawImage(img,0,0,s,s);"
    "c.toBlob(function(b){"
    "var a=document.createElement('a');"
    "a.href=URL.createObjectURL(b);a.download='thermal.jpg';"
    "document.body.appendChild(a);a.click();"
    "setTimeout(function(){URL.revokeObjectURL(a.href);a.remove();},500);"
    "},'image/jpeg',0.9);};"
    "img.src='" SNAPSHOT_PATH "?dl='+Date.now();"
    "}"
    "var liveOn=false,liveBusy=false,lastLive=0;"
    "function setLive(cb){liveOn=cb.checked;lastLive=0;liveTick();}"
    "function liveTick(){"
    "if(!liveOn||liveBusy)return;"
    "var n=Date.now(),d=n-lastLive;"
    "if(d<250){setTimeout(liveTick,250-d);return;}"
    "lastLive=n;liveBusy=true;"
    "var img=document.getElementById('thermal');"
    "var next=new Image();"
    "next.onload=function(){img.src=next.src;liveBusy=false;liveTick();};"
    "next.onerror=function(){liveBusy=false;liveTick();};"
    "next.src='" SNAPSHOT_PATH "?t='+n;"
    "}"
    "</script>"
    "</body></html>";

static const char PAGE_OFFLINE[] PROGMEM =
    "offline</th></tr></table></center><br>";

// ---------------------------------------------------------------------------
// /wifi - set STA credentials (stored in EEPROM, applied on reboot)
// ---------------------------------------------------------------------------
static void handleWifi() {
  if (webServer.method() == HTTP_POST) {
    String ssid = webServer.arg("ssid");
    String pass = webServer.arg("pass");
    ssid.trim();
    if (ssid.length() == 0) {
      webServer.send(400, "text/plain", "SSID required");
      return;
    }
    wifiSaveCfg(ssid.c_str(), pass.c_str());
    webServer.send(200, "text/html",
                   "<!DOCTYPE html><html><body><h2>Saved. Rebooting...</h2>"
                   "</body></html>");
    delay(500);
    ESP.restart();
    return;
  }

  WifiCfg cfg;
  bool haveCfg = wifiLoadCfg(cfg);

  webServer.sendHeader("Connection", "close");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", "");

  webServer.sendContent_P(PAGE_HEAD);
  webServer.sendContent_P(PSTR("network settings</th></tr></table></center>"));

  char tmp[400];
  snprintf(tmp, sizeof(tmp),
           "<center><form method=POST action=/wifi>"
           "<table>"
           "<tr><th>Station mode (connect to your router)</th></tr>"
           "<tr><td>SSID:</td><td><input name=ssid maxlength=32 "
           "value=\"%s\"></td></tr>"
           "<tr><td>Password:</td><td><input name=pass maxlength=64 "
           "value=\"%s\"></td></tr>"
           "<tr><td></td><td><input type=submit value=\"Save & restart\"></td></tr>"
           "</table></form>"
           "<p>%s</p>"
           "<a href='/'>Back</a></center>",
           haveCfg ? cfg.ssid : "",
           haveCfg ? cfg.pass : "",
           haveCfg ? "Current mode: STATION (stored config will be used at boot)" :
                     "Current mode: ACCESS POINT (no stored config)");
  webServer.sendContent(tmp);

  webServer.sendContent_P(PAGE_FOOT_JS);
  webServer.sendContent("");
  webServer.client().stop();
}

// ---------------------------------------------------------------------------
// /snapshot.jpg  - encode the shared RGB565 frame to JPEG and stream it
// ---------------------------------------------------------------------------
static void handleSnapshot() {
  // bail out early if the requesting client already went away
  if (!webServer.client().connected()) return;

  // Transient buffers: a snapshot request is serialized (one at a time) so the
  // ~9.3 KB of scratch memory is only on the heap while a request is active and
  // frees up the static RAM (jpegBuf/jpgSrc) the rest of the time.
  // JPEGENC samples 8x8 MCUs unconditionally, so a 42x42 source would over-read
  // the frame buffer by ~500 bytes. Encode from a padded 48x48 (8-multiple)
  // copy with the right/bottom edges replicated, so the output stays 42x42.
  const int JPG_PAD = 48;
  uint16_t *jpgSrc = (uint16_t *)malloc(JPG_PAD * JPG_PAD * sizeof(uint16_t));
  uint8_t *jpegBuf = (uint8_t *)malloc(JPEG_BUF_SIZE);
  if (!jpgSrc || !jpegBuf) {
    free(jpgSrc);
    free(jpegBuf);
    webServer.send(503, "text/plain", "Out of memory");
    return;
  }

  // copy the shared 42x42 frame into the padded source with edge replication
  for (int r = 0; r < INTERP_RES; r++) {
    memcpy(&jpgSrc[r * JPG_PAD], frame[r], INTERP_RES * sizeof(uint16_t));
    for (int c = INTERP_RES; c < JPG_PAD; c++)
      jpgSrc[r * JPG_PAD + c] = frame[r][INTERP_RES - 1];
  }
  uint16_t *lastRow = &jpgSrc[(INTERP_RES - 1) * JPG_PAD];
  for (int r = INTERP_RES; r < JPG_PAD; r++)
    memcpy(&jpgSrc[r * JPG_PAD], lastRow, JPG_PAD * sizeof(uint16_t));

  JPEGENCODE enc;
  int rc = jpeg.open(jpegBuf, JPEG_BUF_SIZE);
  if (rc == 0) {
    jpeg.encodeBegin(&enc, INTERP_RES, INTERP_RES, JPEGE_PIXEL_RGB565,
                     JPEGE_SUBSAMPLE_444, JPEGE_Q_MED);
    jpeg.addFrame(&enc, (uint8_t *)jpgSrc, JPG_PAD * 2);
    rc = jpeg.close();
  }
  free(jpgSrc);
  if (rc <= 0) {
    free(jpegBuf);
    webServer.send(500, "text/plain", "JPEG encode failed");
    return;
  }

  webServer.setContentLength(rc);
  webServer.send(200, "image/jpeg", "");
  // Send in small chunks with yield() in between: a synchronous write of the
  // whole buffer can stall up to the TCP timeout on a slow/disconnected
  // client, which trips the ESP8266 soft watchdog and reboots the device.
  WiFiClient client = webServer.client();
  const uint8_t *p = jpegBuf;
  size_t left = rc;
  while (left > 0 && client.connected()) {
    size_t n = (left > 1024) ? 1024 : left;
    size_t sent = client.write(p, n);
    if (sent == 0) break; // client gone / send failed
    p += sent;
    left -= sent;
    yield();
  }
  free(jpegBuf);
}

// ---------------------------------------------------------------------------
// /  - index page (live snapshot + statistics)
// ---------------------------------------------------------------------------
static void handleIndex() {
  webServer.sendHeader("Connection", "close");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", "");

  webServer.sendContent_P(PAGE_HEAD);

  if (AMGok) {
    webServer.sendContent_P(PAGE_IMG);

    // statistics from the Q4 sensor readings
    int16_t mn = INT16_MAX, mx = INT16_MIN;
    int32_t sum = 0;
    for (int i = 0; i < 64; i++) {
      int16_t v = pixelsQ4[i];
      sum += v;
      if (v < mn) mn = v;
      if (v > mx) mx = v;
    }
    float avg = sum / 64.0f / Q4_SCALE;
    float ctr = pixelsQ4[27] / (float)Q4_SCALE;

    char tmp[48];
    snprintf(tmp, sizeof(tmp), "<tr><td>Min temp: %.1f &#8451;</td></tr>",
             mn / (float)Q4_SCALE);
    webServer.sendContent(tmp);
    snprintf(tmp, sizeof(tmp), "<tr><td>Max temp: %.1f &#8451;</td></tr>",
             mx / (float)Q4_SCALE);
    webServer.sendContent(tmp);
    snprintf(tmp, sizeof(tmp), "<tr><td>Avg temp: %.1f &#8451;</td></tr>",
             avg);
    webServer.sendContent(tmp);
    snprintf(tmp, sizeof(tmp), "<tr><td>Center temp: %.1f &#8451;</td></tr>",
             ctr);
    webServer.sendContent(tmp);
  } else {
    webServer.sendContent_P(PAGE_OFFLINE);
  }

  char tb[160];
  snprintf(tb, sizeof(tb), "<tr><td>Battery: %.2f V</td></tr>",
           getBatteryVolts());
  webServer.sendContent(tb);
  snprintf(tb, sizeof(tb), "<tr><td>Mode: %s</td></tr>",
           apMode ? "AP" : "STA");
  webServer.sendContent(tb);
  snprintf(tb, sizeof(tb), "<tr><td>Last reset: %s</td></tr>",
           ESP.getResetReason().c_str());
  webServer.sendContent(tb);
  snprintf(tb, sizeof(tb), "<tr><td>Free heap: %u B / max block %u B</td></tr>",
           (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxFreeBlockSize());
  webServer.sendContent(tb);
  snprintf(tb, sizeof(tb), "<tr><td>Reset info: %s</td></tr>",
           ESP.getResetInfo().c_str());
  webServer.sendContent(tb);

  webServer.sendContent_P(PSTR("<tr><th><div><a href='/update'>Update</a>"
                               " | <a href='/wifi'>WiFi settings</a>"
                               "</th></tr>"));
  webServer.sendContent_P(PAGE_FOOT_JS);

  webServer.sendContent("");
  webServer.client().stop();
}

// ---------------------------------------------------------------------------
// onNotFound: in AP mode any request (captive portal detection probes like
// connectivitycheck.gstatic.com/generate_204, unknown paths) is redirected to
// the index page. Without this the phone shows "no internet" but never opens
// the portal. In STA mode no DNS capture is needed, plain 404 is fine.
// ---------------------------------------------------------------------------
static void handleNotFound() {
  if (apMode) {
    String loc = "http://";
    loc += APIP.toString();
    loc += "/";
    webServer.sendHeader("Location", loc, true);
    webServer.send(302, "text/plain", "");
  } else {
    webServer.send(404, "text/plain", "Not found");
  }
}

// ---------------------------------------------------------------------------
bool webBegin() {
  WifiCfg cfg;
  if (wifiLoadCfg(cfg)) {
    // STA config present: try to join the network. Wait briefly for a result.
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid, cfg.pass);
    uint32_t start = millis();
    while (millis() - start < 10000 && WiFi.status() != WL_CONNECTED) {
      delay(50);
    }
    if (WiFi.status() == WL_CONNECTED) {
      apMode = false;
    } else {
      apMode = true;
    }
  }

  if (apMode) {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(APIP, APIP, IPAddress(255, 255, 255, 0));
    // empty APPASSWORD ("" ) == open AP, proven captive-portal behaviour on the
    // reference project; a >=8 char password protects the network
    WiFi.softAP(APSSID, APPASSWORD);

    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", APIP);
  }

  webServer.on("/", HTTP_GET, handleIndex);
  webServer.on(SNAPSHOT_PATH, HTTP_GET, handleSnapshot);
  webServer.on("/wifi", HTTP_GET, handleWifi);
  webServer.on("/wifi", HTTP_POST, handleWifi);
  webServer.onNotFound(handleNotFound);
  otaBegin(webServer);
  webServer.begin();
  return apMode;
}

void webLoop() {
  if (apMode) dnsServer.processNextRequest();
  webServer.handleClient();
  otaLoop();
}
