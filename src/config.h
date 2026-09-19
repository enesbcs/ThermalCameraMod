#ifndef CONFIG_H
#define CONFIG_H

// WiFi AP settings
#define APSSID      "ThermoCam"
#define APPASSWORD  ""    // empty password = open AP (reliable captive portal)
                          // set a >=8 char password to protect the AP
#define SUBTITLE    "AMG8833"

// Interpolated thermal frame resolution
#define INTERP_RES  42      // final frame size (42x42)
#define INTERP_QUOT 6       // (INTERP_RES / 8) + 1 = 6

// Q4 fixed point: temperature stored as value = temp * 4 (0.25 C resolution)
#define Q4_SHIFT    2
#define Q4_SCALE    (1 << Q4_SHIFT)   // 4

// Temperature scale bounds, expressed in Q4 units (temp * 4)
#define MIN_TEMP_Q4  0        //  0 C
#define MAX_TEMP_Q4  (80 * 4) // 80 C

// TFT block scaling: 3x -> 126x126 image centered on the 128x128 ST7735
#define TFT_SCALE    3
#define DISP_SIZE    (INTERP_RES * TFT_SCALE)  // 126
#define TFT_IMG_SIZE 128 // full panel size, image leaves a 1px black margin

// ST7735 GRAM window offset: this module's visible 128x128 window does not
// start at the GREENTAB128 (0,0) origin, so the far column/row are never
// written and show power-on noise. Offsetting our own CASET/RASET covers the
// visible cells while the skipped cells fall outside the visible window.
// Right edge fixed with col=1; bottom still noisy -> row needs a larger offset.
#define GRAM_COL_OFFS 1
#define GRAM_ROW_OFFS 2

// Web snapshot
#define SNAPSHOT_PATH "/snapshot.jpg"

// Battery ADC (through 100k resistor on A0)
#define AMG_ANA_PIN  A0

#endif
