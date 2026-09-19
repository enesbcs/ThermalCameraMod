/*

  https://forum.pjrc.com/threads/48113

  This program is for upsizing an 8 x 8 array of thermal camera readings
  it will size up by 10x and display to a 240 x 320
  interpolation is linear and "good enough" given the display is a 5-6-5 color palet
  Total final array is an array of 70 x 70 of internal points only

  Revisions
  1.0     Kasprzak      Initial code

  Branches
  1.0     Deiss         Code modified for Wemos D1 Mini, vertical display, temperature measurement at center, battery measurement
  1.1     Deiss         Exchanged TFT driver for better performance regarding framerate
  1.2     Deiss         Changed datatype of MINTEMP and MAXTEMP to fit pixels, corrected pin assignment
  1.3     Deiss         Bugfix MINTEMP and MAXTEMP typecast instead of different datatype
  1.4     enesbcs       Changed hardware to 128x128 display, software adjusted according it


  MCU                       Wemos D1 Mini close
  Display                   https://www.aliexpress.com/af/st7735-1.44-spi-display.html?trafficChannel=af&d=y&CatId=0&SearchText=st7735+1.44+spi+display&ltype=affiliate&SortType=price_asc&groupsort=1&page=1
  Thermal sensor            https://learn.adafruit.com/adafruit-amg8833-8x8-thermal-camera-sensor/overview
  sensor library            https://github.com/adafruit/Adafruit_AMG88xx
  equations generated from  http://web-tech.ga-usa.com/2012/05/creating-a-custom-hot-to-cold-temperature-color-gradient-for-use-with-rrdtool/index.html

  Pinouts
  MCU         Device
  D1          AMG SDA
  D2          AMG SCL
  Gnd         Dispaly GND, AMG Gnd
  3v3         Dispaly Vcc,Display LED,Display RST, AMG Vcc
  D0          Dispaly T_IRQ
  D3          Display D/C
  D8          Display CS
  D7          Display SDI
  D6          Dispaly SDO
  D5          Display SCK
  A0          Battery+ through 100k resistor

*/

//#include <Fonts/FreeMonoBoldOblique12pt7b.h>
#include <TFT_eSPI.h>
//#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <SPI.h>

#define TFT_CS         15
#define TFT_RST        -1
#define TFT_DC         2

#include <Adafruit_AMG88xx.h>       // thermal camera lib

TFT_eSPI Display = TFT_eSPI();
//Adafruit_ST7735 Display = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// create some colors for the keypad buttons
#define C_BLUE Display.color565(0,0,255)
#define C_RED Display.color565(255,0,0)
#define C_GREEN Display.color565(0,255,0)
#define C_WHITE Display.color565(255,255,255)
#define C_BLACK Display.color565(0,0,0)
#define C_LTGREY Display.color565(200,200,200)
#define C_DKGREY Display.color565(80,80,80)
#define C_GREY Display.color565(127,127,127)

#include <ESP8266WiFi.h>
#include <ESP8266WiFiAP.h>
#include <ESP8266WiFiGeneric.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WiFiScan.h>
#include <ESP8266WiFiSTA.h>
#include <ESP8266WiFiType.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ElegantOTA.h>
#include "bitmap.h"
#define ROTATE180

#define APSSID "ThermoCam" // SSID & Title
#define APPASSWORD "12345678" // Blank password = Open AP
#define SUBTITLE "AMG8833"

#define INTERP_RES 42  // interpolation lowered to 42 from 70

#define INTERP_QUOT 6 //(INTERP_RES/8)+1

const byte DNS_PORT = 53;
DNSServer dnsServer;
ESP8266WebServer webServer(80);
const byte HTTP_CODE = 200;
//IPAddress APIP(192, 168, 4, 1);
IPAddress APIP(172, 217, 28, 1);

unsigned long scrtime = 0;
unsigned long scaletime = 0;

// start with some initial colors
uint16_t MinTemp = 18;
uint16_t MaxTemp = 38;

// variables for interpolated colors
byte red, green, blue;

// variables for row/column interpolation
byte i, j, k, row, col, incr;
float intPoint, val, a, b, c, d, ii;
byte aLow, aHigh;

// size of a display "pixel"
byte BoxWidth = 3;
byte BoxHeight = 3;
byte AMGok = 0;

int x, y;

// variable to toggle the display grid
int ShowGrid = -1;

// array for the 8 x 8 measured pixels
float pixels[64];

// array for the interpolated array
float HDTemp[80][80];
char buf[20];

// create the camara object
Adafruit_AMG88xx ThermalSensor;

// my fast yet effective color interpolation routine
uint16_t GetColor(float val) {

  /*
    pass in value and figure out R G B
    several published ways to do this I basically graphed R G B and developed simple linear equations
    again a 5-6-5 color display will not need accurate temp to R G B color calculation

    equations based on
    http://web-tech.ga-usa.com/2012/05/creating-a-custom-hot-to-cold-temperature-color-gradient-for-use-with-rrdtool/index.html

  */

  red = constrain(255.0 / (c - b) * val - ((b * 255.0) / (c - b)), 0, 255);

  if ((val > MinTemp) & (val < a)) {
    green = constrain(255.0 / (a - MinTemp) * val - (255.0 * MinTemp) / (a - MinTemp), 0, 255);
  }
  else if ((val >= a) & (val <= c)) {
    green = 255;
  }
  else if (val > c) {
    green = constrain(255.0 / (c - d) * val - (d * 255.0) / (c - d), 0, 255);
  }
  else if ((val > d) | (val < a)) {
    green = 0;
  }

  if (val <= b) {
    blue = constrain(255.0 / (a - b) * val - (255.0 * b) / (a - b), 0, 255);
  }
  else if ((val > b) & (val <= d)) {
    blue = 0;
  }
  else if (val > d) {
    blue = constrain(240.0 / (MaxTemp - d) * val - (d * 240.0) / (MaxTemp - d), 0, 240);
  }

  // use the displays color mapping function to get 5-6-5 color palet (R=5 bits, G=6 bits, B-5 bits)
  return Display.color565(red, green, blue);

}

String footer() {
  return "<footer>"
         "<div class=\"footer\""
         " <p><b>Author:</b> <a href='https://bitekmindenhol.blog.hu/'>NS Tech</a> <b>2020</b></p>"
         "</div>"
         "</footer>";
}

String header() {
  String a = String(APSSID);
  String CSS = "article { background: #f2f2f2; padding: 1.3em; }"
               "body { color: #333; font-family: Century Gothic, sans-serif; font-size: 18px; line-height: 24px; margin: 0; padding: 0; }"
               "div { padding: 0.35em; }"
               "h1 { margin: 0.5em 0 0 0; padding: 0.5em; }"
               "input { border-radius: 0; border: 1px solid #555555; }"
               "label { color: #333; display: block; font-style: italic; font-weight: bold; }"
               "nav { background: #0066ff; color: #fff; display: block; font-size: 1.3em; padding: 1em; }"
               "nav b { display: block; font-size: 1.5em; margin-bottom: 0.5em; } "
               "textarea { width: 100%; }"
               ".button {"
               "background-color: #4CAF50;"
               "border: 1px solid black;"
               "border-radius: 6px;"
               "color: white;"
               "padding: 15px 32px;"
               "text-align: center;"
               "text-decoration: none;"
               "display: inline-block;"
               "font-size: 32px;width:100%;height:5em;"
               "margin: 4px 2px;"
               "cursor: pointer;"
               "}"
               ".buttonb { background-color: #555555; }"
               ".footer {"
               "position: fixed;"
               "left: 0;"
               "bottom: 0;"
               "width: 100%;"
               "background-color: #0066ff;"
               "color: white;"
               "text-align: center;"
               "font-family: \"Verdana\", Sans, serif;"
               "border-radius: 0px;"
               "height: 25px"
               "}";
  String h = "<!DOCTYPE html><html>"
             "<head><title>" + a + " :: " + SUBTITLE + "</title>"
             "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
             "<style>" + CSS + "</style></head>"
             "<body><nav><b>" + a + "</b> " + SUBTITLE + "</nav><br>";
  return h;
}

String index() {
  float batteryval = 0;
  String retstr = header() + "<center><table><tr><th>AMG8833 sensor: ";
  if (AMGok == 1) {
    retstr += "online</th></tr><tr><th>Live image<br><img src='http://" + APIP.toString() + "/snapshot.bmp"
              + "' width='" + String(INTERP_RES * 4) + "' height='" + String(INTERP_RES * 4) + "'></th></tr>";

    float aMinTemp = 80;
    float aMaxTemp = 0;
    float aAvgTemp = 0;

    for (i = 0; i < 64; i++) {
      aAvgTemp += pixels[i];
      if (pixels[i] < aMinTemp) {
        aMinTemp = pixels[i];
      }
      if (pixels[i] > aMaxTemp) {
        aMaxTemp = pixels[i];
      }
    }
    aAvgTemp = (aAvgTemp / 64);
    retstr += "<tr><td>Min temp: " + String(aMinTemp) + " &#8451;</td></tr>";
    retstr += "<tr><td>Max temp: " + String(aMaxTemp) + " &#8451;</td></tr>";
    retstr += "<tr><td>Avg temp: " + String(aAvgTemp) + " &#8451;</td></tr>";
    retstr += "<tr><td>Center temp: " + String(pixels[27]) + " &#8451;</td></tr>";

  } else {
    retstr += "offline</th></tr>";
  }
  batteryval = round(analogRead(A0)/2.4915)/100.0;
  retstr += "<tr><td>Battery: " + String(batteryval) + " V</td></tr>";
  
  return retstr +
         + "<tr><th><div><a href='/update'>Update</a></th></tr>"
         + "</table></center><br>" + footer();
}

// function to get the cutoff points in the temp vs RGB graph
void Getabcd() {

  a = MinTemp + (MaxTemp - MinTemp) * 0.2121;
  b = MinTemp + (MaxTemp - MinTemp) * 0.3182;
  c = MinTemp + (MaxTemp - MinTemp) * 0.4242;
  d = MinTemp + (MaxTemp - MinTemp) * 0.8182;

}

void handle_bmp(void)
{

  WiFiClient client = webServer.client();
  if (!client.connected())
  {
    return;
  }
  uint16_t linepix[INTERP_RES];
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-disposition: inline; filename=snapshot.bmp\r\n";
  response += "Content-type: image/bmp\r\n\r\n";
  webServer.sendContent(response);
  char *bmp = bmp_create_header565(INTERP_RES, INTERP_RES);
  client.write(bmp, sizeof(bitmap565));
  free(bmp);
#ifndef ROTATE180
  for (row = 0; row < INTERP_RES; row ++) {
    for (col = 0; col < INTERP_RES; col++) {
      linepix[col] = GetColor(HDTemp[INTERP_RES - row][INTERP_RES - col]);
    }
    client.write((char *)linepix, sizeof(linepix));
  }
#else
  for (row = 0; row < INTERP_RES; row ++) {
    for (col = 0; col < INTERP_RES; col++) {
      linepix[col] = GetColor(HDTemp[row][col]);
    }
    client.write((char *)linepix, sizeof(linepix));
  }
#endif

}

void setup() {

  Serial.begin(115200);

  AMGok = 0;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(APIP, APIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(APSSID, APPASSWORD);

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", APIP);

  //webServer.onNotFound([]() {
  webServer.on("/", []() {
    webServer.send(HTTP_CODE, "text/html", index());
  });
  webServer.on("/snapshot.bmp", HTTP_GET, handle_bmp);
  Serial.println("Setup wifi");
  ElegantOTA.begin(&webServer);
  webServer.begin();

  // start the display and set the background to black
  Display.begin();
  Display.fillScreen(C_BLACK);

  // set display rotation (you may need to change to 0 depending on your display
#ifdef ROTATE180
  Display.setRotation(6);
#endif

  // show a cute splash screen (paint text twice to show a little shadow

  // Display.setFont(&FreeMonoBoldOblique12pt7b);
  // Display.setFont(DroidSans_40);
  Display.setTextSize(2);
  Display.setCursor(12, 11);
  Display.setTextColor(C_WHITE, C_BLACK);
  Display.print("Thermal");

  //Display.setFont(DroidSans_40);
  Display.setCursor(10, 10);
  Display.setTextColor(C_BLUE);
  Display.print("Thermal");

  //Display.setFont(Arial_48_Bold_Italic);
  Display.setCursor(42, 51);
  Display.setTextColor(C_WHITE, C_BLACK);
  Display.print("Camera");

  //Display.setFont(Arial_48_Bold_Italic);
  Display.setCursor(40, 50);
  Display.setTextColor(C_RED);
  Display.print("Camera");


  // let sensor boot up
  bool status = ThermalSensor.begin();
  delay(100);
  Display.setTextSize(1);
  if (status) {
    if (ThermalSensor.readThermistor() < 0) { // check if sensor really works
      Serial.println("Sensor failed");
      status = false;
    }
  }
  // check status and display results
  if (!status) {

    //Display.setFont(DroidSans_20);
    Display.setCursor(10, 70);
    Display.setTextColor(C_RED, C_BLACK);
    Display.print("Sensor: FAIL");
    delay(500);
    //Display.setFont(DroidSans_20);
    Display.setCursor(10, 70);
    Display.setTextColor(C_BLACK, C_BLACK);
    Display.print("Sensor: FAIL");
    delay(500);

  }
  else {
    //Display.setFont(DroidSans_20);
    Display.setCursor(10, 70);
    Display.setTextColor(C_GREEN, C_BLACK);
    Display.print("Sensor: FOUND");
    AMGok = 1;
  }

  if (AMGok == 1) {
    // read the camera for initial testing
    ThermalSensor.readPixels(pixels);

    // check status and display results
    if (pixels[0] < 0) {
      //Display.setFont(DroidSans_20);
      Display.setCursor(10, 90);
      Display.setTextColor(C_RED, C_BLACK);
      Display.print("Readings: FAIL");
      delay(500);
      //Display.setFont(DroidSans_20);
      Display.setCursor(10, 90);
      Display.setTextColor(C_BLACK, C_BLACK);
      Display.print("Readings: FAIL");
      delay(500);

      AMGok = 0;
    }
    else {
      // Display.setFont(DroidSans_20);
      Display.setCursor(10, 90);
      Display.setTextColor(C_GREEN, C_BLACK);
      Display.print("Readings: OK");
      delay(1000);
      AMGok = 1;
    }

    // set display rotation and clear the fonts..the rotation of this display is a bit weird

    //Display.setFontAdafruit();
    Display.fillScreen(C_BLACK);
    // Display.setFont();

    // get the cutoff points for the color interpolation routines
    // note this function called when the temp scale is changed
    Getabcd();
    scaletime = millis() + 5000;
  }
}

// Draw measured value
void drawMeasurement() {
  // Measure and print center temperature
  uint16_t centerTemp = pixels[27];

  Display.setCursor(1, 1);
  Display.setTextColor(C_WHITE, C_BLACK);
  Display.setTextSize(1);
  sprintf(buf, "%s:%2d", "T", centerTemp);
  Display.print(buf);
}


// interplation function to create 70 columns for 8 rows
void InterpolateRows() {

  // interpolate the 8 rows (interpolate the 70 column points between the 8 sensor pixels first)
  for (row = 0; row < 8; row ++) {
    for (col = 0; col < INTERP_RES; col ++) {
      // get the first array point, then the next
      // also need to bump by 8 for the subsequent rows
      aLow =  int(col / INTERP_QUOT + (row * 8));
      aHigh = aLow + 1;
      // get the amount to interpolate for each of the 10 columns
      // here were doing simple linear interpolation mainly to keep performace high and
      // display is 5-6-5 color palet so fancy interpolation will get lost in low color depth
      intPoint =   float(( pixels[aHigh] - pixels[aLow] ) / (INTERP_QUOT * 1.0) );
      // determine how much to bump each column (basically 0-9)
      incr = col % INTERP_QUOT;
      // find the interpolated value
      val = float(intPoint * incr ) + pixels[aLow];
      // store in the 70 x 70 array
      // since display is pointing away, reverse row to transpose row data
      HDTemp[ int((7 - row) * INTERP_QUOT)][col] = val;

    }
  }
}

// interplation function to interpolate 70 columns from the interpolated rows
void InterpolateCols() {

  // then interpolate the 70 rows between the 8 sensor points
  for (col = 0; col < INTERP_RES; col ++) {
    for (row = 0; row < INTERP_RES; row ++) {
      // get the first array point, then the next
      // also need to bump by 8 for the subsequent cols
      aLow =  int((row / INTERP_QUOT ) * INTERP_QUOT);
      aHigh = aLow + INTERP_QUOT;
      // get the amount to interpolate for each of the 10 columns
      // here were doing simple linear interpolation mainly to keep performace high and
      // display is 5-6-5 color palet so fancy interpolation will get lost in low color depth
      intPoint =   float(( HDTemp[aHigh][col] - HDTemp[aLow][col] ) / (INTERP_QUOT * 1.0) );
      // determine how much to bump each column (basically 0-9)
      incr = row % INTERP_QUOT;
      // find the interpolated value
      val = float(intPoint * incr ) +  HDTemp[aLow][col];
      // store in the 70 x 70 array
      HDTemp[ row ][col] = val;
    }
  }
}

// function to display the results
void DisplayGradient() {

  // rip through 70 rows
  for (row = 0; row < INTERP_RES; row ++) {

    // fast way to draw a non-flicker grid--just make every 10 pixels 2x2 as opposed to 3x3
    // drawing lines after the grid will just flicker too much
    if (ShowGrid < 0) {
      BoxWidth = 3;
    }
    else {
      if ((row % 10 == 9) ) {
        BoxWidth = 2;
      }
      else {
        BoxWidth = 3;
      }
    }
    // then rip through each 70 cols
    for (col = 0; col < INTERP_RES; col++) {

      // fast way to draw a non-flicker grid--just make every 10 pixels 2x2 as opposed to 3x3
      if (ShowGrid < 0) {
        BoxHeight = 3;
      }
      else {
        if ( (col % 10 == 9)) {
          BoxHeight = 2;
        }
        else {
          BoxHeight = 3;
        }
      }
      // finally we can draw each the 70 x 70 points, note the call to get interpolated color
      //      Display.fillRect((row * 3) + 15, (col * 3) + 15, BoxWidth, BoxHeight, GetColor(HDTemp[row][col]));
      Display.fillRect((row * 3), (col * 3), BoxWidth, BoxHeight, GetColor(HDTemp[row][col]));
      if (col < 3) {
        drawMeasurement();
      }

    }
  }

}


// function to automatically set the max / min scale based on adding an offset to the average temp from the 8 x 8 array
// you could also try setting max and min based on the actual max min
void SetTempScale() {

  uint16_t aMinTemp = 80;
  uint16_t aMaxTemp = 0;
  for (i = 0; i < 64; i++) {

    if ((uint16_t)pixels[i] < aMinTemp) {
      aMinTemp = (uint16_t)pixels[i];
    }
    if ((uint16_t)pixels[i] > aMaxTemp) {
      aMaxTemp = (uint16_t)pixels[i];
    }

  }
  aMaxTemp = aMaxTemp + 2.0;
  aMinTemp = aMinTemp + 2.0;

  if ((aMaxTemp > MaxTemp + 2) || (aMaxTemp < MaxTemp - 4)) {
    MaxTemp = aMaxTemp;
  }
  if ((aMinTemp < MinTemp - 2) || (aMinTemp > MinTemp + 2)) {
    MinTemp = aMinTemp;
  }
  if (MaxTemp - MinTemp < 5) {
    MaxTemp = MinTemp + 5;
  }
  if (MinTemp < 0) {
    MinTemp = 0;
  }
  if (MaxTemp > 80) {
    MaxTemp = 80;
  }

  Getabcd();
}


void loop() {
  if (AMGok == 1) {
    if (scrtime < millis()) {
      // read the sensor
      ThermalSensor.readPixels(pixels);
      scrtime = millis() + 100;
      // now that we have an 8 x 8 sensor array
      // interpolate to get a bigger screen
      InterpolateRows();

      // now that we have row data with 70 columns
      // interpolate each of the 70 columns
      // forget Arduino..no where near fast enough..Teensy at > 72 mhz is the starting point
      InterpolateCols();

      // display the 70 x 70 array
      DisplayGradient();

      if (scaletime < millis()) {
        SetTempScale();
        scaletime = millis() + 5000;
      }

    }
  } else {
    delay(1);
  }
  dnsServer.processNextRequest();
  webServer.handleClient();
}

// end of code
