#include "main.h"
#include "web.h"
#include "config.h"

#include <Wire.h>
#include <cmath>

TFT_eSPI Display = TFT_eSPI();
Adafruit_AMG88xx ThermalSensor;

int16_t pixelsQ4[64];          // raw 8x8 readings, temp * 4
uint16_t frame[INTERP_RES][INTERP_RES]; // shared RGB565 frame
uint16_t colorLUT[MAX_TEMP_Q4 + 1];     // color lookup by Q4 temp

int16_t MinTempQ4 = 18 * Q4_SCALE;      // 18 C
int16_t MaxTempQ4 = 38 * Q4_SCALE;      // 38 C
int8_t ShowGrid = -1;
bool AMGok = false;

// TFT render helpers
static uint16_t scanline[TFT_IMG_SIZE]; // one 3x-zoomed line + black right border
static uint16_t zeros[TFT_IMG_SIZE];    // black line for grid + bottom margin
static uint8_t sensorRaw[128];          // raw 8x8 readings (2 bytes/pixel)

static unsigned long scrtime = 0;
static unsigned long scaletime = 0;

// ---------------------------------------------------------------------------
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

float getBatteryVolts() {
  return round(analogRead(AMG_ANA_PIN) / 2.4915f) / 100.0f;
}

// ---------------------------------------------------------------------------
// Regenerate the color LUT whenever the Min/Max scale changes. This replaces
// the per-pixel float math of the original GetColor() with a single table
// lookup per pixel.
// ---------------------------------------------------------------------------
void updateColorLUT() {
  int32_t range = MaxTempQ4 - MinTempQ4;
  int32_t aQ = MinTempQ4 + range * 2121 / 10000;
  int32_t bQ = MinTempQ4 + range * 3182 / 10000;
  int32_t cQ = MinTempQ4 + range * 4242 / 10000;
  int32_t dQ = MinTempQ4 + range * 8182 / 10000;

  int32_t cb = cQ - bQ;     // red ramp denominator
  int32_t aMin = aQ - MinTempQ4;
  int32_t cd = cQ - dQ;     // green ramp down denominator
  int32_t ab = aQ - bQ;     // blue ramp up denominator
  int32_t Md = MaxTempQ4 - dQ;

  for (int i = 0; i <= MAX_TEMP_Q4; i++) {
    int32_t v = i;
    int red, green, blue;

    red = (int)(255 * (v - bQ) / cb);
    if (red < 0) red = 0; else if (red > 255) red = 255;

    if (v > MinTempQ4 && v < aQ) {
      green = (int)(255 * (v - MinTempQ4) / aMin);
    } else if (v >= aQ && v <= cQ) {
      green = 255;
    } else if (v > cQ) {
      green = (int)(255 * (v - dQ) / cd);
    } else {
      green = 0;
    }
    if (green < 0) green = 0; else if (green > 255) green = 255;

    if (v <= bQ) {
      blue = (int)(255 * (v - bQ) / ab);
    } else if (v > bQ && v <= dQ) {
      blue = 0;
    } else {
      blue = (int)(240 * (v - dQ) / Md);
    }
    if (blue < 0) blue = 0; else if (blue > 240) blue = 240;

    colorLUT[i] = rgb565((uint8_t)red, (uint8_t)green, (uint8_t)blue);
  }
}

// ---------------------------------------------------------------------------
// Read the sensor and quantize the raw 12-bit readings into Q4 fixed point
// once. The AMG8833 pixel LSB = 0.25 C = 1 Q4 step, so the conversion is a
// pure integer sign-extend of the 12-bit two's-complement value - no floats.
// ---------------------------------------------------------------------------
static void readSensor() {
  ThermalSensor.readPixelsRaw(sensorRaw, 64); // one bulk 128-byte I2C read
  for (int i = 0; i < 64; i++) {
    uint16_t v = (uint16_t)sensorRaw[i << 1] | ((uint16_t)sensorRaw[(i << 1) + 1] << 8);
    pixelsQ4[i] = (int16_t)(v << 4) >> 4; // sign-extend the 12-bit value to Q4
  }
}

// ---------------------------------------------------------------------------
// 8x8 -> 42x42 pure-integer interpolation, then one LUT lookup per cell into
// the shared RGB565 frame buffer.
// ---------------------------------------------------------------------------
static int16_t anchors[8][INTERP_RES];

// Per-output-position interpolation data. The 8->42 geometry is fixed, so the
// per-pixel index/frac division-modulo is precomputed once here (in setup());
// the frame builder only does a table lookup plus a single Q8 multiply+shift.
struct InterpPos {
  uint8_t index;  // source sensor index (0..6; index+1 is the segment end)
  uint8_t weight; // Q8 fixed-point fraction toward the next sensor (0..255)
};
static InterpPos interp[INTERP_RES];

void buildFrame() {
  // 1. Interpolate each sensor row of 8 values up to INTERP_RES columns.
  //    (vertical mirror to match the ROTATE180 data orientation)
  for (int row = 0; row < 8; row++) {
    const int16_t *src = &pixelsQ4[row * 8];
    int16_t *dst = anchors[7 - row];
    for (int c = 0; c < INTERP_RES; c++) {
      uint8_t sLow = interp[c].index;
      uint8_t sHigh = sLow + (sLow < 7);
      dst[c] = (int16_t)(src[sLow] + (((int32_t)(src[sHigh] - src[sLow]) *
                                       interp[c].weight) >> 8));
    }
  }

  // 2. Interpolate vertically between the anchor rows and look up the color.
  //    Rows are mirrored top/bottom (up/down came out flipped); columns keep
  //    their direction because the row mirror alone fixes orientation.
  for (int r = 0; r < INTERP_RES; r++) {
    uint8_t a = interp[r].index;
    uint8_t b = a + (a < 7);
    int w = interp[r].weight;
    const int16_t *lo = anchors[a];
    const int16_t *hi = anchors[b];
    uint16_t *frow = frame[INTERP_RES - 1 - r];
    for (int c = 0; c < INTERP_RES; c++) {
      int32_t t = lo[c] + (((int32_t)(hi[c] - lo[c]) * w) >> 8);
      if (t < 0) t = 0;
      else if (t > MAX_TEMP_Q4) t = MAX_TEMP_Q4;
      frow[c] = colorLUT[t];
    }
  }
}

// ---------------------------------------------------------------------------
// Push the frame to the 128x128 ST7735 using scanline transfers instead of
// thousands of fillRect() calls.
// ---------------------------------------------------------------------------
void DisplayGradient() {
  const bool grid = (ShowGrid >= 0);
  uint16_t *sl = scanline;

  // the image is 126x126 on the 128x128 panel; always rewrite the full panel
  // width so the 2 right-hand columns never show stale/garbled GRAM content
  memset(scanline, 0, TFT_IMG_SIZE * sizeof(uint16_t));
  memset(zeros, 0, TFT_IMG_SIZE * sizeof(uint16_t));

  Display.startWrite();
  for (int r = 0; r < INTERP_RES; r++) {
    const uint16_t *frow = frame[r];

    // build the 3x-zoomed scanline (126 wide, right 2px stay black)
    for (int c = 0; c < INTERP_RES; c++) {
      uint16_t px = frow[c];
      // Encode the panel's inverse transform before the raw fast-path
      // pushPixels: the wire carries each pixel byte-swapped (so the panel
      // decodes bgr(swap16(px))). Pre-swapping R<->B fields and the two bytes
      // makes the displayed color equal the value the webUI JPEG decodes.
      px = (uint16_t)(((px & 0x001F) << 11) | (px & 0x07E0) |
                      ((px >> 11) & 0x001F)); // bgr()
      px = (uint16_t)((px >> 8) | (px << 8)); // swap16()
      int o = c * TFT_SCALE;
      sl[o] = px;
      sl[o + 1] = px;
      sl[o + 2] = px;
    }

    if (grid) {
      // vertical separators every 10 columns
      for (int c = 9; c < INTERP_RES; c += 10) {
        sl[c * TFT_SCALE] = 0;
        sl[c * TFT_SCALE + 1] = 0;
      }
      bool horiz = (r % 10 == 9); // 1px horizontal separator
      Display.setAddrWindow(GRAM_COL_OFFS, r * TFT_SCALE + GRAM_ROW_OFFS,
                            TFT_IMG_SIZE, TFT_SCALE);
      for (int y = 0; y < TFT_SCALE; y++) {
        Display.pushPixels((horiz && y == TFT_SCALE - 1) ? zeros : sl,
                           TFT_IMG_SIZE);
      }
    } else {
      Display.setAddrWindow(GRAM_COL_OFFS, r * TFT_SCALE + GRAM_ROW_OFFS,
                            TFT_IMG_SIZE, TFT_SCALE);
      for (int y = 0; y < TFT_SCALE; y++) {
        Display.pushPixels(sl, TFT_IMG_SIZE);
      }
    }
  }

  // bottom 2 rows: the 126-row image leaves rows 126..127 untouched
  Display.setAddrWindow(GRAM_COL_OFFS, DISP_SIZE + GRAM_ROW_OFFS,
                        TFT_IMG_SIZE, TFT_IMG_SIZE - DISP_SIZE);
  for (int y = DISP_SIZE; y < TFT_IMG_SIZE; y++)
    Display.pushPixels(zeros, TFT_IMG_SIZE);
  Display.endWrite();
}

// ---------------------------------------------------------------------------
// After the gradient, draw the center-temperature readout once per frame.
// Shows the raw center pixel with 1 decimal (same value the web page reports).
// The reading is not filtered so it stays accurate; its raw 0.25 C steps show
// up as light flicker of the last digit, which is acceptable - a deadband or
// a black reserved band would hide real changes or waste display space.
// Note: must run every frame - the gradient redraws the whole panel, so the
// readout has to be re-rendered on top even when the value is unchanged.
// ---------------------------------------------------------------------------
static void drawMeasurement() {
  char buf[20];
  sprintf(buf, "T:%3.1f", pixelsQ4[27] / (float)Q4_SCALE);
  Display.setCursor(3, 3);
  Display.setTextColor(TFT_WHITE, TFT_BLACK);
  Display.setTextSize(1);
  Display.print(buf);
}

// ---------------------------------------------------------------------------
// Auto-adjust the temperature scale every ~5 s (as the original did).
// ---------------------------------------------------------------------------
static void SetTempScale() {
  int16_t aMin = INT16_MAX, aMax = INT16_MIN;
  for (int i = 0; i < 64; i++) {
    int16_t v = pixelsQ4[i];
    if (v < aMin) aMin = v;
    if (v > aMax) aMax = v;
  }

  int16_t tMax = aMax + 2 * Q4_SCALE;
  int16_t tMin = aMin + 2 * Q4_SCALE;

  if (tMax > MaxTempQ4 + 2 * Q4_SCALE || tMax < MaxTempQ4 - 4 * Q4_SCALE) {
    MaxTempQ4 = tMax;
  }
  if (tMin < MinTempQ4 - 2 * Q4_SCALE || tMin > MinTempQ4 + 2 * Q4_SCALE) {
    MinTempQ4 = tMin;
  }

  // Clamp as a pair, then enforce the minimum 5C span LAST so the range can
  // never collapse below 5*Q4_SCALE (updateColorLUT() divides by the range:
  // a too-small range would make the ramp denominators 0 and trip an integer
  // divide-by-zero -> exception/reboot on uniform hot scenes).
  if (MinTempQ4 < MIN_TEMP_Q4) MinTempQ4 = MIN_TEMP_Q4;
  if (MaxTempQ4 > MAX_TEMP_Q4) MaxTempQ4 = MAX_TEMP_Q4;
  if (MaxTempQ4 - MinTempQ4 < 5 * Q4_SCALE) {
    if (MinTempQ4 + 5 * Q4_SCALE > MAX_TEMP_Q4) {
      MinTempQ4 = MAX_TEMP_Q4 - 5 * Q4_SCALE; // slide the pair down together
    }
    MaxTempQ4 = MinTempQ4 + 5 * Q4_SCALE;
  }

  updateColorLUT();
}

// ---------------------------------------------------------------------------
void setup() {
  // display up, black background
  Display.begin();
  // 90 deg counter-clockwise so the display + PCB fit the enclosure
  Display.setRotation(3);
  Display.fillScreen(TFT_BLACK);

  // precompute the constant 8x8 -> INTERP_RES x INTERP_RES mapping
  for (int i = 0; i < INTERP_RES; i++) {
    interp[i].index = (uint8_t)(i / INTERP_QUOT);
    interp[i].weight = (uint8_t)(((i % INTERP_QUOT) * 256) / INTERP_QUOT);
  }

  // simple splash
  Display.setTextSize(2);
  Display.setCursor(12, 11);
  Display.setTextColor(TFT_WHITE, TFT_BLACK);
  Display.print("Thermal");
  Display.setCursor(42, 51);
  Display.setTextColor(TFT_WHITE, TFT_BLACK);
  Display.print("Camera");

  // allow the sensor to boot
  Wire.begin();
  Wire.setClock(400000); // AMG8833 Fast-mode: cuts the 128-byte bulk read time
  bool status = ThermalSensor.begin();
  delay(100);
  Display.setTextSize(1);
  if (status && ThermalSensor.readThermistor() < 0) {
    status = false;
  }

  if (!status) {
    Display.setCursor(10, 70);
    Display.setTextColor(TFT_RED, TFT_BLACK);
    Display.print("Sensor: FAIL");
  } else {
    readSensor();
    if (pixelsQ4[0] < 0) {
      Display.setCursor(10, 70);
      Display.setTextColor(TFT_RED, TFT_BLACK);
      Display.print("Readings: FAIL");
    } else {
      Display.setCursor(10, 70);
      Display.setTextColor(TFT_GREEN, TFT_BLACK);
      Display.print("Sensor: FOUND");
      AMGok = true;

      Display.fillScreen(TFT_BLACK);
      updateColorLUT();
      scaletime = millis() + 5000;
    }
  }

  // network + captive portal + OTA last so it does not block sensor init
  webBegin();
  scrtime = millis() + 100;
}

// ---------------------------------------------------------------------------
void loop() {
  if (AMGok) {
    if (scrtime < millis()) {
      readSensor();          // 8x8 raw in Q4
      buildFrame();          // full 42x42 RGB565 frame (interp + LUT)

      // scale update before rendering so the LUT is fresh
      if (scaletime < millis()) {
        SetTempScale();
        scaletime = millis() + 5000;
      }

      DisplayGradient();     // fast scanline render
      drawMeasurement();     // once per frame
      yield();               // feed the watchdog after the SPI burst

      scrtime = millis() + 100;  // ~10 FPS
    }
  } else {
    delay(1);
  }

  webLoop();
}
