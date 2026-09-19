#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Adafruit_AMG88xx.h>
#include "config.h"

// Display + sensor (defined in main.cpp)
extern TFT_eSPI Display;
extern Adafruit_AMG88xx ThermalSensor;

// 8x8 raw sensor readings in Q4 fixed point (value = celsius * 4)
extern int16_t pixelsQ4[64];

// Shared 42x42 RGB565 frame buffer, produced once per frame and consumed by
// both the TFT renderer and the JPEG snapshot handler.
extern uint16_t frame[INTERP_RES][INTERP_RES];

// Color lookup table indexed by Q4 temperature (0..MAX_TEMP_Q4)
extern uint16_t colorLUT[MAX_TEMP_Q4 + 1];

// Current auto-adjusted scale bounds (Q4)
extern int16_t MinTempQ4, MaxTempQ4;

// Grid overlay on/off (>0 = on)
extern int8_t ShowGrid;

// Sensing state for the web page
extern bool AMGok;

// Battery voltage in volts (reads the ADC)
float getBatteryVolts();

// Build the RGB565 frame from the sensor pixels (interpolation + LUT)
void buildFrame();

// Push the frame to the TFT using fast scanline transfers
void DisplayGradient();

// Regenerate the color LUT after Min/Max scale changed
void updateColorLUT();

#endif
