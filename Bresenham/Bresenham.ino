#include <math.h>
#include "OLED_Driver.h"
#include "GUI_paint.h"
#include "DEV_Config.h"
#include "Debug.h"
#include "ImageData.h"

// Demonstrates Bresenham's line algorithm (Paint_DrawLine in GUI_Paint.cpp)
// against every octant: a rotating radar sweep, then a full radial burst
// that reveals a spoke at a time, then holds before clearing and repeating.

UBYTE *BlackImage;

const int CX = 64, CY = 32;
const int SWEEP_RADIUS = 30;
const int BURST_RADIUS = 30;
const int BURST_SPOKES = 60;   // one spoke every 6 degrees
const int SWEEPS_BEFORE_BURST = 2;

enum Mode { SWEEP, BURST_FILL, BURST_HOLD };

void setup() {
  System_Init();
  Serial.print(F("OLED_Init()...\r\n"));
  OLED_1IN51_Init();
  Driver_Delay_ms(500);
  OLED_1IN51_Clear();

  UWORD Imagesize = ((OLED_1IN51_WIDTH % 8 == 0) ? (OLED_1IN51_WIDTH / 8) : (OLED_1IN51_WIDTH / 8 + 1)) * OLED_1IN51_HEIGHT;
  if ((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL) {
    Serial.print("Failed to apply for black memory...\r\n");
    return;
  }
  Paint_NewImage(BlackImage, OLED_1IN51_WIDTH, OLED_1IN51_HEIGHT, 270, BLACK);
  Paint_SelectImage(BlackImage);
  Paint_Clear(BLACK);
}

void loop() {
  static Mode mode = SWEEP;
  static float sweepAngle = 0;
  static int sweepTurns = 0;
  static int burstIndex = 0;
  static unsigned long holdStart = 0;

  switch (mode) {
    case SWEEP: {
      Paint_Clear(BLACK);
      int ex = CX + (int)round(cos(sweepAngle) * SWEEP_RADIUS);
      int ey = CY + (int)round(sin(sweepAngle) * SWEEP_RADIUS);
      Paint_DrawLine(CX, CY, ex, ey, WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
      Paint_DrawPoint(ex, ey, WHITE, DOT_PIXEL_2X2, DOT_STYLE_DFT);
      OLED_1IN51_Display(BlackImage);

      sweepAngle += 0.12;
      if (sweepAngle > 2 * PI) {
        sweepAngle -= 2 * PI;
        sweepTurns++;
        if (sweepTurns >= SWEEPS_BEFORE_BURST) {
          sweepTurns = 0;
          mode = BURST_FILL;
          burstIndex = 0;
          Paint_Clear(BLACK);
        }
      }
      Driver_Delay_ms(35);
      break;
    }

    case BURST_FILL: {
      float a = (2.0 * PI * burstIndex) / BURST_SPOKES;
      int ex = CX + (int)round(cos(a) * BURST_RADIUS);
      int ey = CY + (int)round(sin(a) * BURST_RADIUS);
      Paint_DrawLine(CX, CY, ex, ey, WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
      OLED_1IN51_Display(BlackImage);

      burstIndex++;
      if (burstIndex >= BURST_SPOKES) {
        mode = BURST_HOLD;
        holdStart = millis();
      }
      Driver_Delay_ms(25);
      break;
    }

    case BURST_HOLD: {
      if (millis() - holdStart > 2500) {
        Paint_Clear(BLACK);
        mode = SWEEP;
      }
      break;
    }
  }
}
